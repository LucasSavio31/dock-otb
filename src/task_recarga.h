#pragma once
// =============================================================
//  task_recarga.h — V5.3
//  Ciclo autônomo de recarga: posição 1 → 2 → 3
//
//  Fluxo por posição:
//    1. Detecta sensor I2C (gNivel[ch].leituraOk) + caneta NFC presente
//       — se ausente, pula posição (cartucho NÃO é requisito)
//    2. Estabiliza 5s (re-verifica presença a cada 500 ms)
//    3. Lê nível:
//        < 10 % → recarrega
//       ≥ 60 % → pula (já carregado)
//       10-60 % → sem ação
//    4. Sequência de recarga:
//        Abre válvula correspondente (1/2/3)
//        Bomba 85 % → ao atingir 50 %: reduz para 40 %
//        Ao atingir 85 %: bomba OFF → 200 ms → válvula OFF
//    5. Grava NFC:
//        Caneta  → ciclos++, vida-- (INATIVO quando vida=0)
//        Cartucho → ciclos++, vida-=5 (INATIVO quando vida=0)
//        gCartLevel[cor] -= 5
//
//  Protocolo serial:
//    RECHARGE_STATUS:<ch>,<nivel>,<duty>,<estado>
//    Estados: NO_DEVICE | DETECTING | SKIP_FULL | SKIP_LEVEL_OK |
//             RUNNING | TAPERING | DONE | TIMEOUT | SENSOR_ERR | ABORTED
// =============================================================
#include "shared.h"
#include "task_erros.h"
#include <Preferences.h>

// ── Globals (definidos aqui, extern em shared.h) ─────────────
RechargeInfo      gRecharge    = { RechargeInfo::IDLE, 0, 0.0f, 0, 0 };
SemaphoreHandle_t mutexRecharge = nullptr;

// ── Constantes ───────────────────────────────────────────────
#define RECHARGE_TIMEOUT_MS      90000   // timeout de segurança (ms)
#define RECHARGE_LEVEL_NEED      10.0f   // nível crítico: abaixo → recarrega
#define RECHARGE_LEVEL_SKIP      60.0f   // nível bom: acima → pula
#define RECHARGE_LEVEL_TAPER     50.0f   // ponto de redução de duty
#define RECHARGE_LEVEL_DONE      85.0f   // nível de conclusão
#define RECHARGE_DUTY_FILL       85      // duty inicial (%)
#define RECHARGE_DUTY_TAPER      40      // duty reduzido após 50 % (%)
#define RECHARGE_STABILIZE_MS    5000    // estabilização antes de checar nível (ms)
#define RECHARGE_CYCLE_IDLE_MS    2000   // pausa entre varreduras completas (ms)
#define TAG_STATUS_INATIVO       5       // status de tag inativa (vida = 0)

// ── Helpers internos ─────────────────────────────────────────

static void _rechargeControl(ControleCmd::Type type, uint8_t payload = 0) {
  ControleCmd cmd{ type, payload };
  xQueueSend(qControleCmd, &cmd, pdMS_TO_TICKS(100));
}

// Parada de emergência — corta bomba e válvula sem delay
static void _rechargeAbort(uint8_t ch) {
  _rechargeControl(ControleCmd::BOMBA_OFF);
  _rechargeControl(ControleCmd::VALVULA_OFF, ch + 1);
}

// Finalização normal — bomba para, aguarda 200 ms, fecha válvula
static void _rechargeFinish(uint8_t ch) {
  _rechargeControl(ControleCmd::BOMBA_OFF);
  vTaskDelay(pdMS_TO_TICKS(200));
  _rechargeControl(ControleCmd::VALVULA_OFF, ch + 1);
}

static void _rechargeIncrementPersistentCount() {
  if (!mutexNVS || xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) != pdTRUE) return;
  Preferences prefs;
  prefs.begin("otb-dock", false);
  prefs.putUInt("rechg_cnt",    prefs.getUInt("rechg_cnt",    0) + 1);
  prefs.putUInt("total_cycles", prefs.getUInt("total_cycles", 0) + 1);
  prefs.end();
  xSemaphoreGive(mutexNVS);
}

static void _gravarTagNFC(uint8_t readerIdx, const TagData &d) {
  SerialCmd sc{};
  sc.type      = SerialCmd::CMD_GRAVAR;
  sc.readerIdx = readerIdx;
  sc.payload   = d;
  xQueueSend(qSerialCmd, &sc, pdMS_TO_TICKS(100));
}

// Grava caneta pós-recarga: ciclos++, vida--, INATIVO se vida zerar
static void _salvarTagCaneta(uint8_t ch) {
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) != pdTRUE) return;
  if (!gTagReaders[ch].valid || !gTagReaders[ch].presente) {
    xSemaphoreGive(mutexTag);
    return;
  }
  TagData d = gTagReaders[ch].data;
  xSemaphoreGive(mutexTag);

  d.ciclos++;
  if (d.vida > 0) d.vida--;
  d.status = (d.vida == 0) ? TAG_STATUS_INATIVO : 1;

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    gTagReaders[ch].data = d;
    xSemaphoreGive(mutexTag);
  }
  _gravarTagNFC(ch, d);

  if (d.vida == 0)
    logdbPublishf("Recarga", "CanetatInativa", LOG_WARN,
                  "Caneta %u inativa (vida=0, ciclos=%u).", (unsigned)(ch + 1), d.ciclos);

  logdbPublishf("Recarga", "TagCaneta", LOG_SUCCESS,
                "Caneta %u: ciclos=%u vida=%u status=%u gravados no NFC.",
                (unsigned)(ch + 1), d.ciclos, d.vida, d.status);
}

// Grava cartucho pós-recarga: ciclos++, vida-=5, INATIVO se vida zerar
static void _salvarTagCartucho(uint8_t ch) {
  uint8_t rc = ch + 3;
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) != pdTRUE) return;
  if (!gTagReaders[rc].valid || !gTagReaders[rc].presente) {
    xSemaphoreGive(mutexTag);
    return;
  }
  TagData d = gTagReaders[rc].data;
  xSemaphoreGive(mutexTag);

  d.ciclos++;
  if (d.vida >= 5) d.vida -= 5;
  else             d.vida  = 0;

  if (d.vida == 0) {
    d.status = TAG_STATUS_INATIVO;
    // Zera o nível virtual na cor correspondente
    if (d.cor >= COR_VERMELHO && d.cor <= COR_AMARELO) gCartLevel[d.cor] = 0;
    logdbPublishf("Recarga", "CartuchoInativo", LOG_WARN,
                  "Cartucho %u inativo (vida=0, ciclos=%u).", (unsigned)(ch + 1), d.ciclos);
  }

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    gTagReaders[rc].data = d;
    xSemaphoreGive(mutexTag);
  }
  _gravarTagNFC(rc, d);
  logdbPublishf("Recarga", "TagCartucho", LOG_SUCCESS,
                "Cartucho %u: ciclos=%u vida=%u gravados no NFC.",
                (unsigned)(ch + 1), d.ciclos, d.vida);
}

// Verifica se a posição tem sensor I2C + caneta NFC + cartucho NFC ativos.
// Preenche *outLevel com o nível atual se retornar true.
static bool _posicaoApta(uint8_t ch, float *outLevel) {
  // Sensor I2C: deve estar lendo
  bool  sensorOk = false;
  float level    = 0.0f;
  if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
    sensorOk = gNivel[ch].leituraOk;
    level    = gNivel[ch].nivelPct;
    xSemaphoreGive(mutexNivel);
  }
  if (!sensorOk) return false;

  // Caneta: presente, dados válidos, vida > 0, não INATIVO
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
    bool ok = gTagReaders[ch].presente &&
              gTagReaders[ch].valid    &&
              gTagReaders[ch].data.vida   > 0 &&
              gTagReaders[ch].data.status != TAG_STATUS_INATIVO;
    xSemaphoreGive(mutexTag);
    if (!ok) return false;
  } else return false;

  // Cartucho: presente, dados válidos, vida > 0, não INATIVO, cor com nível > 0
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
    uint8_t rc = ch + 3;
    bool ok = gTagReaders[rc].presente &&
              gTagReaders[rc].valid    &&
              gTagReaders[rc].data.vida   > 0 &&
              gTagReaders[rc].data.status != TAG_STATUS_INATIVO;
    if (ok) {
      TagCor cor = gTagReaders[rc].data.cor;
      if (cor >= COR_VERMELHO && cor <= COR_AMARELO && gCartLevel[cor] == 0)
        ok = false;
    }
    xSemaphoreGive(mutexTag);
    if (!ok) return false;
  } else return false;

  if (outLevel) *outLevel = level;
  return true;
}

// ── Task ──────────────────────────────────────────────────────
void taskRecarga(void *param) {
  vTaskDelay(pdMS_TO_TICKS(8000));
  Serial.println("[Recarga] Ciclo iniciado.");

  for (;;) {
    if (gBloqueado) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

    // ── Modo manual: aguarda START do serial/dashboard ─────────
    uint8_t chStart = 0, chEnd = 3;
    if (gOpMode == OP_MANUAL) {
      RechargeCmd cmd;
      if (xQueueReceive(qRechargeCmd, &cmd, pdMS_TO_TICKS(200)) != pdTRUE) continue;
      if (cmd.type != RechargeCmd::START || cmd.channel > 2) continue;
      chStart = cmd.channel;
      chEnd   = cmd.channel + 1;
    } else {
      // Standalone: drena STARTs pendentes (firmware controla)
      { RechargeCmd c; while (xQueueReceive(qRechargeCmd, &c, 0) == pdTRUE) {} }
    }

    // ── Varredura posições ─────────────────────────────────────
    for (uint8_t ch = chStart; ch < chEnd; ch++) {

      // ── 1. Detecção: sensor I2C + caneta NFC ─────────────
      float levelPct = 0.0f;
      if (!_posicaoApta(ch, &levelPct)) {
        Serial.printf("RECHARGE_STATUS:%d,0,0,NO_DEVICE\n", ch);
        continue;
      }

      // ── 2. Estabilização 5 s ──────────────────────────────
      Serial.printf("RECHARGE_STATUS:%d,%.1f,0,DETECTING\n", ch, levelPct);
      // Atualiza gRecharge para DETECTING — nextion navega para tela 7 imediatamente
      if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
        gRecharge = {};
        gRecharge.status   = RechargeInfo::DETECTING;
        gRecharge.channel  = ch;
        gRecharge.levelPct = levelPct;
        xSemaphoreGive(mutexRecharge);
      }
      bool stable = true;
      for (uint32_t t = 0; t < RECHARGE_STABILIZE_MS && stable; t += 500) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!_posicaoApta(ch, &levelPct)) { stable = false; break; }
        RechargeCmd sc;
        if (xQueuePeek(qRechargeCmd, &sc, 0) == pdTRUE && sc.type == RechargeCmd::STOP) {
          xQueueReceive(qRechargeCmd, &sc, 0);
          stable = false;
        }
      }
      if (!stable) continue;

      // Relê nível após estabilização
      if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
        levelPct = gNivel[ch].nivelPct;
        xSemaphoreGive(mutexNivel);
      }

      // ── 3. Verificação de nível ───────────────────────────
      if (levelPct >= RECHARGE_LEVEL_SKIP) {
        Serial.printf("RECHARGE_STATUS:%d,%.1f,0,SKIP_FULL\n", ch, levelPct);
        logdbPublishf("Recarga", "Pulada", LOG_INFO,
                      "Pos %u pulada: nivel=%.1f%% >= %.0f%%.",
                      (unsigned)(ch + 1), levelPct, RECHARGE_LEVEL_SKIP);
        continue;
      }
      if (levelPct >= RECHARGE_LEVEL_NEED) {
        // Entre 10 % e 60 %: sem ação
        Serial.printf("RECHARGE_STATUS:%d,%.1f,0,SKIP_LEVEL_OK\n", ch, levelPct);
        continue;
      }

      // ── 4. Recarrega ─────────────────────────────────────
      logdbPublishf("Recarga", "Inicio", LOG_INFO,
                    "Pos=%u nivel=%.1f%% — iniciando recarga.", (unsigned)(ch + 1), levelPct);
      erroClear(ERR_E301);

      // Captura dados de caneta e cartucho para a tela anam_rec
      RechargeInfo ri = {};
      ri.status   = RechargeInfo::RUNNING;
      ri.channel  = ch;
      ri.levelPct = levelPct;
      ri.dutyPct  = RECHARGE_DUTY_FILL;
      ri.elapsedMs = 0;
      if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
        snprintf(ri.penId,      sizeof(ri.penId),      "%s", gTagReaders[ch].data.id);
        snprintf(ri.penSerial,  sizeof(ri.penSerial),  "%s", gTagReaders[ch].data.serial);
        ri.penCiclos = gTagReaders[ch].data.ciclos;
        uint8_t rc = ch + 3;
        snprintf(ri.cartId,     sizeof(ri.cartId),     "%s", gTagReaders[rc].data.id);
        snprintf(ri.cartSerial, sizeof(ri.cartSerial), "%s", gTagReaders[rc].data.serial);
        xSemaphoreGive(mutexTag);
      }
      if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
        gRecharge = ri;
        xSemaphoreGive(mutexRecharge);
      }

      // Abre válvula correspondente à posição
      _rechargeControl(ControleCmd::VALVULA_ON, ch + 1);
      vTaskDelay(pdMS_TO_TICKS(300));

      // Bomba a 85 %
      _rechargeControl(ControleCmd::BOMBA_DUTY, RECHARGE_DUTY_FILL);

      RechargeInfo::Status state = RechargeInfo::RUNNING;
      uint32_t startMs     = millis();
      uint8_t  sensorErrCnt = 0;
      bool     tapered     = false;
      bool     success     = false;
      uint8_t  duty        = RECHARGE_DUTY_FILL;
      Serial.printf("RECHARGE_STATUS:%d,%.1f,%d,RUNNING\n", ch, levelPct, duty);

      // ── Malha de controle ────────────────────────────────
      while (state == RechargeInfo::RUNNING || state == RechargeInfo::TAPERING) {

        // Abort por STOP: verifica a cada 100 ms para resposta quase imediata
        bool aborted = false;
        for (uint8_t tick = 0; tick < 5 && !aborted; tick++) {
          vTaskDelay(pdMS_TO_TICKS(100));
          RechargeCmd sc;
          if (xQueueReceive(qRechargeCmd, &sc, 0) == pdTRUE && sc.type == RechargeCmd::STOP) {
            _rechargeAbort(ch);
            state = RechargeInfo::ABORTED;
            if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
              gRecharge.status = RechargeInfo::ABORTED;
              xSemaphoreGive(mutexRecharge);
            }
            Serial.printf("RECHARGE_STATUS:%d,%.1f,0,ABORTED\n", ch, levelPct);
            logdbPublishf("Recarga", "Abortada", LOG_WARN,
                          "Pos=%u abortada por comando.", (unsigned)(ch + 1));
            aborted = true;
          }
        }
        if (aborted) break;

        uint32_t elapsed = millis() - startMs;

        // Timeout de segurança
        if (elapsed > RECHARGE_TIMEOUT_MS) {
          _rechargeAbort(ch);
          erroSetar(ERR_E301);
          state = RechargeInfo::TIMEOUT;
          Serial.printf("RECHARGE_STATUS:%d,%.1f,0,TIMEOUT\n", ch, levelPct);
          logdbPublishf("Recarga", "Timeout", LOG_ERROR,
                        "Timeout pos=%u (%lus).", (unsigned)(ch + 1), (unsigned long)(elapsed / 1000));
          break;
        }

        // Lê sensor
        bool sensorOk = false;
        if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
          sensorOk = gNivel[ch].leituraOk;
          levelPct = gNivel[ch].nivelPct;
          xSemaphoreGive(mutexNivel);
        }

        if (!sensorOk) {
          if (++sensorErrCnt >= 5) {
            _rechargeAbort(ch);
            erroSetar(ERR_E211 + ch);
            state = RechargeInfo::SENSOR_ERR;
            Serial.printf("RECHARGE_STATUS:%d,0,0,SENSOR_ERR\n", ch);
            logdbPublishf("Recarga", "Falha", LOG_ERROR,
                          "Sensor err pos=%u.", (unsigned)(ch + 1));
            break;
          }
        } else {
          sensorErrCnt = 0;

          if (levelPct >= RECHARGE_LEVEL_DONE) {
            // Bomba OFF → 200 ms → válvula OFF
            _rechargeFinish(ch);
            duty    = 0;
            state   = RechargeInfo::DONE;
            success = true;
            logdbPublishf("Recarga", "Concluida", LOG_SUCCESS,
                          "Pos=%u concluida nivel=%.1f%%.", (unsigned)(ch + 1), levelPct);

          } else if (!tapered && levelPct >= RECHARGE_LEVEL_TAPER) {
            // Reduz duty a 40 % ao atingir 50 %
            tapered = true;
            duty    = RECHARGE_DUTY_TAPER;
            _rechargeControl(ControleCmd::BOMBA_DUTY, duty);
            state   = RechargeInfo::TAPERING;
            Serial.printf("RECHARGE_STATUS:%d,%.1f,%d,TAPERING\n", ch, levelPct, duty);
          }
        }

        // Atualiza global
        if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
          gRecharge = { state, ch, levelPct, duty, elapsed };
          xSemaphoreGive(mutexRecharge);
        }

        if (state == RechargeInfo::RUNNING || state == RechargeInfo::TAPERING)
          Serial.printf("RECHARGE_STATUS:%d,%.1f,%d,%s\n", ch, levelPct, duty,
                        state == RechargeInfo::TAPERING ? "TAPERING" : "RUNNING");
        else if (state == RechargeInfo::DONE)
          Serial.printf("RECHARGE_STATUS:%d,%.1f,0,DONE\n", ch, levelPct);
      }

      // ── 5. Pós-recarga ────────────────────────────────────
      if (success) {
        _rechargeIncrementPersistentCount();
        gRechargeCount++;

        // Decrementa nível virtual do cartucho pela cor da tag
        TagCor cor = COR_DESCONHECIDA;
        if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
          uint8_t rc = ch + 3;
          if (rc < 6 && gTagReaders[rc].valid) cor = gTagReaders[rc].data.cor;
          xSemaphoreGive(mutexTag);
        }
        if (cor >= COR_VERMELHO && cor <= COR_AMARELO) {
          if (gCartLevel[cor] >= 5) gCartLevel[cor] -= 5;
          else                       gCartLevel[cor]  = 0;
          // Força refresh imediato de j1/j2/j3 no Nextion
          { TagEvent ev = {}; ev.type = TagEvent::TAG_PRESENTE;
            ev.readerIdx = (uint8_t)(ch + 3);
            xQueueSend(qNextionData, &ev, 0); }
        }

        // Grava dados nas tags NFC
        _salvarTagCaneta(ch);
        _salvarTagCartucho(ch);

        // Reinicia leitores de caneta 1/2/3 para nova detecção
        nfcPenReinitPending = true;

        if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
          gRecharge.status = RechargeInfo::IDLE;
          xSemaphoreGive(mutexRecharge);
        }

        // 5s entre recargas sequenciais — Nextion retorna à dock_status neste período
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue; // pula o vTaskDelay(500) abaixo
      }

      vTaskDelay(pdMS_TO_TICKS(500)); // pausa curta entre posições sem recarga
    }

    if (gOpMode != OP_MANUAL) {
      vTaskDelay(pdMS_TO_TICKS(RECHARGE_CYCLE_IDLE_MS));
    }
  }
}
