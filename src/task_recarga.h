#pragma once
// =============================================================
//  task_recarga.h — V5
//  Lógica de recarga de canetas
//
//  Algoritmo (malha aberta):
//    0 – 60 % de nível → bomba 100 % duty
//   60 – 95 % de nível → duty cai linearmente de 100 % → 20 %
//       ≥ 95 %          → bomba OFF, válvula fecha, DONE
//  Timeout 60 s         → para tudo, sinaliza ERR_E301
//
//  Funciona standalone (sem dashboard) ou com dashboard conectado.
//  Publica "RECHARGE_STATUS:<ch>,<nivel>,<duty>,<estado>" a cada 500 ms.
// =============================================================
#include "shared.h"
#include "task_erros.h"
#include <Preferences.h>

// ── Globals (definidos em main.cpp) ──────────────────────────
RechargeInfo      gRecharge    = { RechargeInfo::IDLE, 0, 0.0f, 0, 0 };
SemaphoreHandle_t mutexRecharge = nullptr;

#define RECHARGE_TIMEOUT_MS   60000   // 60 s
#define RECHARGE_LEVEL_TAPER  60.0f   // % a partir do qual reduz duty
#define RECHARGE_LEVEL_DONE   95.0f   // % para considerar cheio
#define RECHARGE_DUTY_MAX    100
#define RECHARGE_DUTY_MIN     20

// ── Helper ────────────────────────────────────────────────────
static void _rechargeControl(ControleCmd::Type type, uint8_t payload = 0) {
  ControleCmd cmd{ type, payload };
  xQueueSend(qControleCmd, &cmd, pdMS_TO_TICKS(100));
}

static void _rechargeStop(uint8_t channel) {
  _rechargeControl(ControleCmd::BOMBA_OFF);
  _rechargeControl(ControleCmd::VALVULA_OFF, channel + 1);
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

// Envia CMD_GRAVAR para taskNFC escrever de volta na tag de um leitor
static void _gravarTagNFC(uint8_t readerIdx, const TagData &d) {
  SerialCmd sc{};
  sc.type      = SerialCmd::CMD_GRAVAR;
  sc.readerIdx = readerIdx;
  sc.payload   = d;
  xQueueSend(qSerialCmd, &sc, pdMS_TO_TICKS(100));
}

// Atualiza tag da caneta após recarga concluída
static void _salvarTagCaneta(uint8_t ch) {
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) != pdTRUE) return;
  if (!gTagReaders[ch].valid || !gTagReaders[ch].presente) {
    xSemaphoreGive(mutexTag);
    return;
  }
  TagData d = gTagReaders[ch].data;
  xSemaphoreGive(mutexTag);

  d.ciclos++;
  d.status = 1; // OK / carregada

  // Atualiza cache local antes de enviar para fila
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    gTagReaders[ch].data = d;
    xSemaphoreGive(mutexTag);
  }
  _gravarTagNFC(ch, d);
  logdbPublishf("Recarga", "TagCaneta", LOG_SUCCESS,
                "Caneta %u: ciclos=%u status=%u gravados no NFC.", (unsigned)(ch+1), d.ciclos, d.status);
}

// Atualiza tag do cartucho após recarga concluída
static void _salvarTagCartucho(uint8_t ch) {
  uint8_t rc = ch + 3; // leitor do cartucho
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) != pdTRUE) return;
  if (!gTagReaders[rc].valid || !gTagReaders[rc].presente) {
    xSemaphoreGive(mutexTag);
    return;
  }
  TagData d = gTagReaders[rc].data;
  xSemaphoreGive(mutexTag);

  // vida representa % de tinta restante (0-100); decrementa 5 por recarga
  if (d.vida >= 5) d.vida -= 5;
  else             d.vida  = 0;
  d.ciclos++;

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    gTagReaders[rc].data = d;
    xSemaphoreGive(mutexTag);
  }
  _gravarTagNFC(rc, d);
  logdbPublishf("Recarga", "TagCartucho", LOG_SUCCESS,
                "Cartucho %u: vida=%u ciclos=%u gravados no NFC.", (unsigned)(ch+1), d.vida, d.ciclos);
}

// ── Task ──────────────────────────────────────────────────────
void taskRecarga(void *param) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  Serial.println("[Recarga] Task iniciada.");

  RechargeInfo::Status state  = RechargeInfo::IDLE;
  uint8_t  ch          = 0;
  uint32_t startTime   = 0;
  uint8_t  sensorErrCount = 0;

  for (;;) {
    // ── Recebe comando (aguarda até 500 ms) ───────────────────
    RechargeCmd cmd;
    if (xQueueReceive(qRechargeCmd, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {

      if (cmd.type == RechargeCmd::STOP) {
        if (state != RechargeInfo::IDLE) {
          _rechargeStop(ch);
          state = RechargeInfo::IDLE;
          sensorErrCount = 0;
          Serial.println("RECHARGE_STATUS:0,0,0,IDLE");
          logdbPublish("Recarga", "Parada", LOG_WARN, "Recarga interrompida.");
        }

      } else { // START
        if (state != RechargeInfo::IDLE) _rechargeStop(ch);

        ch = cmd.channel;
        sensorErrCount = 0;
        for (uint8_t v = 1; v <= 3; v++)
          _rechargeControl(ControleCmd::VALVULA_OFF, v);
        vTaskDelay(pdMS_TO_TICKS(100));
        _rechargeControl(ControleCmd::VALVULA_ON, ch + 1);
        vTaskDelay(pdMS_TO_TICKS(200));

        _rechargeControl(ControleCmd::BOMBA_DUTY, RECHARGE_DUTY_MAX);

        state     = RechargeInfo::RUNNING;
        startTime = millis();
        Serial.printf("RECHARGE_STATUS:%d,0,%d,RUNNING\n", ch, RECHARGE_DUTY_MAX);
        logdbPublishf("Recarga", "Inicio", LOG_INFO, "Recarga iniciada na caneta %u.", (unsigned)(ch + 1));
      }
    }

    if (state == RechargeInfo::IDLE) continue;

    // ── Verifica timeout ──────────────────────────────────────
    uint32_t elapsed = millis() - startTime;
    if (elapsed > RECHARGE_TIMEOUT_MS) {
      _rechargeStop(ch);
      erroSetar(ERR_E301);
      state = RechargeInfo::TIMEOUT;
      Serial.printf("RECHARGE_STATUS:%d,%.1f,0,TIMEOUT\n", ch, gRecharge.levelPct);
      logdbPublishf("Recarga", "Timeout", LOG_ERROR, "Timeout na caneta %u.", (unsigned)(ch + 1));
      if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
        gRecharge.status = state; xSemaphoreGive(mutexRecharge);
      }
      state = RechargeInfo::IDLE;
      sensorErrCount = 0;
      continue;
    }

    // ── Lê nível do sensor ────────────────────────────────────
    float levelPct = 0.0f;
    float sensorPf = 0.0f;
    bool  sensorOk = false;
    bool  sensorSaturated = false;
    if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
      sensorOk = gNivel[ch].leituraOk;
      levelPct = gNivel[ch].nivelPct;
      sensorPf = gNivel[ch].pFAtual;
      sensorSaturated = gNivel[ch].saturado;
      xSemaphoreGive(mutexNivel);
    }

    // ── Detecção de erro de sensor ────────────────────────────
    if (!sensorOk) {
      sensorErrCount++;
      if (sensorErrCount >= 5) {
        _rechargeStop(ch);
        erroSetar(ERR_E211 + ch);  // E211/E212/E213 por canal
        state = RechargeInfo::SENSOR_ERR;
        Serial.printf("RECHARGE_STATUS:%d,0,0,SENSOR_ERR\n", ch);
        logdbPublishf("Recarga", "Falha", LOG_ERROR, "Erro de sensor na caneta %u.", (unsigned)(ch + 1));
        if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
          gRecharge.status = state; xSemaphoreGive(mutexRecharge);
        }
        state = RechargeInfo::IDLE;
        sensorErrCount = 0;
        continue;
      }
    } else {
      sensorErrCount = 0;
    }

    // ── Calcula duty (malha aberta) ───────────────────────────
    uint8_t duty = RECHARGE_DUTY_MAX;

    if (sensorSaturated || levelPct >= 99.5f) {
      // Sensor saturado → para imediatamente
      _rechargeStop(ch);
      duty  = 0;
      state = RechargeInfo::SATURATED;
      Serial.printf("[Recarga] Sensor saturado CH%d (%.3f pF / %.1f%%)\n", ch, sensorPf, levelPct);
      logdbPublishf("Recarga", "Saturacao", LOG_WARN, "Sensor saturado na caneta %u.", (unsigned)(ch + 1));

    } else if (levelPct >= RECHARGE_LEVEL_DONE) {
      // Tanque cheio → para
      _rechargeStop(ch);
      duty  = 0;
      state = RechargeInfo::DONE;
      logdbPublishf("Recarga", "Concluida", LOG_SUCCESS, "Recarga concluida na caneta %u.", (unsigned)(ch + 1));

    } else if (levelPct >= RECHARGE_LEVEL_TAPER) {
      state = RechargeInfo::TAPERING;
      float ratio = (levelPct - RECHARGE_LEVEL_TAPER) /
                    (RECHARGE_LEVEL_DONE - RECHARGE_LEVEL_TAPER);
      duty = (uint8_t)(RECHARGE_DUTY_MAX - ratio * (RECHARGE_DUTY_MAX - RECHARGE_DUTY_MIN));
      if (duty < RECHARGE_DUTY_MIN) duty = RECHARGE_DUTY_MIN;
      _rechargeControl(ControleCmd::BOMBA_DUTY, duty);
    }

    // ── Atualiza global ───────────────────────────────────────
    if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
      gRecharge.status    = state;
      gRecharge.channel   = ch;
      gRecharge.levelPct  = levelPct;
      gRecharge.dutyPct   = duty;
      gRecharge.elapsedMs = elapsed;
      xSemaphoreGive(mutexRecharge);
    }

    // ── Publica status no serial ──────────────────────────────
    const char* stStr = "RUNNING";
    switch (state) {
      case RechargeInfo::TAPERING:   stStr = "TAPERING";   break;
      case RechargeInfo::DONE:       stStr = "DONE";       break;
      case RechargeInfo::SATURATED:  stStr = "SATURATED";  break;
      default: break;
    }
    Serial.printf("RECHARGE_STATUS:%d,%.1f,%d,%s\n", ch, levelPct, duty, stStr);

    if (state == RechargeInfo::DONE || state == RechargeInfo::SATURATED) {
      _rechargeIncrementPersistentCount();

      // Decrementa nível virtual do cartucho pela cor da tag
      TagCor cor = COR_DESCONHECIDA;
      if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
        uint8_t rc = ch + 3;
        if (rc < 6 && gTagReaders[rc].valid)
          cor = gTagReaders[rc].data.cor;
        xSemaphoreGive(mutexTag);
      }
      if (cor >= COR_VERMELHO && cor <= COR_AMARELO) {
        if (gCartLevel[cor] >= 5) gCartLevel[cor] -= 5;
        else                       gCartLevel[cor]  = 0;
      }
      gRechargeCount++;

      // Grava dados atualizados nas tags NFC da caneta e do cartucho
      _salvarTagCaneta(ch);
      _salvarTagCartucho(ch);

      state = RechargeInfo::IDLE;
      sensorErrCount = 0;
    }
  }
}
