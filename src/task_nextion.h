#pragma once
// =============================================================
//  task_nextion.h — V5
//  Tela status_pen: IdPen1 StatusPen1 CiclosPen1 VidaPen1 SerialPen1
//  Tela dock_status: p0.pic=4 + dados Dockstation
//  Tela erros: tErroCod1..5 tErroDesc1..5 tErroTotal
//  tStatus: codigo do erro ativo em todas as paginas
//  Pagina configs (pg4): btn_purgar(6) btn_val1(10) btn_val2(11) btn_val3(12)
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include "shared.h"
#include "task_erros.h"
#include "esp_chip_info.h"
#include "esp_system.h"

// =========================
// IDs DA PÁGINA configs
// =========================
#define NEXTION_PAGE_CONFIGS    4
#define NEXTION_ID_BTN_PURGAR   6
#define NEXTION_ID_BTN_VAL1    10
#define NEXTION_ID_BTN_VAL2    11
#define NEXTION_ID_BTN_VAL3    12

// =========================
// HELPERS
// =========================
static void _nextionCmd(const char* cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
}

static String _sanitizar(String s) {
  s.replace("\"", "'");
  return s;
}

static void _setText(const char* obj, const char* txt) {
  char cmd[160];
  String s = _sanitizar(String(txt));
  snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", obj, s.c_str());
  _nextionCmd(cmd);
}

static void _setValue(const char* obj, uint32_t value) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "%s.val=%lu", obj, (unsigned long)value);
  _nextionCmd(cmd);
}

static float _nxClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static String _statusTexto(uint8_t s) {
  switch (s) {
    case 0: return "Vazio";
    case 1: return "OK";
    case 2: return "Em uso";
    case 3: return "Bloqueado";
    case 4: return "Erro";
    default: return "Desconhecido";
  }
}

static const char* _rechargeStatusTexto(RechargeInfo::Status status) {
  switch (status) {
    case RechargeInfo::IDLE:       return "Parado";
    case RechargeInfo::RUNNING:    return "Recarregando";
    case RechargeInfo::TAPERING:   return "Finalizando";
    case RechargeInfo::DONE:       return "Concluido";
    case RechargeInfo::TIMEOUT:    return "Timeout";
    case RechargeInfo::SATURATED:  return "Saturado";
    case RechargeInfo::SENSOR_ERR: return "Erro sensor";
    default:                       return "Desconhecido";
  }
}

// =========================
// LIMPA / MOSTRA TAG
// =========================
static void _nextionLimpar() {
  _setText("IdPen1",     "N/A");
  _setText("StatusPen1", "N/A");
  _setText("CiclosPen1", "N/A");
  _setText("VidaPen1",   "N/A");
  _setText("SerialPen1", "N/A");
}

static void _nextionMostrar(const TagData &d) {
  _setText("IdPen1",     d.id);
  _setText("StatusPen1", _statusTexto(d.status).c_str());
  _setText("CiclosPen1", String(d.ciclos).c_str());
  _setText("VidaPen1",   String(d.vida).c_str());
  _setText("SerialPen1", d.serial);
}

// =========================
// DADOS DO SISTEMA — dock_status
// =========================
static void _enviarDadosDock() {
  char tmp[64];
  char cmd[96];
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t s = millis() / 1000;

  auto nx = [&](const char* field, const char* val) {
    String sv = _sanitizar(String(val));
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", field, sv.c_str());
    _nextionCmd(cmd);
  };

  snprintf(tmp, sizeof(tmp), "%d MHz", getCpuFrequencyMhz());
  nx("Freq", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)esp_get_free_heap_size());
  nx("HeapLiv", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)esp_get_minimum_free_heap_size());
  nx("HeapMin", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ESP.getHeapSize());
  nx("HeapTot", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ESP.getFreePsram());
  nx("Sram", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ESP.getFlashChipSize());
  nx("Flash", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ESP.getSketchSize());
  nx("SketTam", tmp);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ESP.getFreeSketchSpace());
  nx("SketLiv", tmp);

  snprintf(tmp, sizeof(tmp), "rev %d", chip.revision);
  nx("Chip", tmp);

  snprintf(tmp, sizeof(tmp), "%d", chip.cores);
  nx("Nucleos", tmp);

  snprintf(tmp, sizeof(tmp), "%s", (chip.features & CHIP_FEATURE_WIFI_BGN) ? "SIM" : "NAO");
  nx("Wifi", tmp);

  snprintf(tmp, sizeof(tmp), "%d", hallRead());
  nx("HallSens", tmp);

  snprintf(tmp, sizeof(tmp), "%d", (int)uxTaskGetNumberOfTasks());
  nx("Tasks", tmp);

  snprintf(tmp, sizeof(tmp), "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600),
           (unsigned long)((s % 3600) / 60),
           (unsigned long)(s % 60));
  nx("Uptime", tmp);

  nx("Serie", gSerialDock);
}

// =========================
// DADOS DE ERROS — pagina erros
// =========================
static void _enviarErros() {
  char tmp[64];
  char cmd[160];

  auto nx = [&](const char* field, const char* val) {
    String sv = _sanitizar(String(val));
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", field, sv.c_str());
    _nextionCmd(cmd);
  };

  uint16_t codigos[5] = {0};
  const char* descs[5] = {nullptr};
  uint8_t count = erroGetAtivos(codigos, descs, 5);

  uint8_t primeiro = erroGetPrimeiro();
  if (primeiro == 0) {
    nx("tStatus", "Pronto");
  } else {
    snprintf(tmp, sizeof(tmp), "E%03d", erroGetCodigo(primeiro));
    nx("tStatus", tmp);
  }

  const char* cf[] = {"tErroCod1", "tErroCod2", "tErroCod3", "tErroCod4", "tErroCod5"};
  const char* df[] = {"tErroDesc1", "tErroDesc2", "tErroDesc3", "tErroDesc4", "tErroDesc5"};

  for (uint8_t i = 0; i < 5; i++) {
    if (i < count) {
      snprintf(tmp, sizeof(tmp), "E%03d", codigos[i]);
      nx(cf[i], tmp);
      nx(df[i], descs[i] ? descs[i] : "");
    } else {
      nx(cf[i], "----");
      nx(df[i], "");
    }
  }

  if (count == 0) {
    nx("tErroTotal", "Nenhum erro ativo");
  } else {
    snprintf(tmp, sizeof(tmp), "%d erro(s) ativo(s)", count);
    nx("tErroTotal", tmp);
  }
}

// =========================
// DASHBOARD RESUMIDA
// Pagina nova sugerida: dashboard
// =========================
static void _enviarDashboard() {
  char tmp[64];
  uint8_t primeiro = erroGetPrimeiro();
  uint16_t codigos[5] = {0};
  const char* descs[5] = {nullptr};
  uint8_t errCount = erroGetAtivos(codigos, descs, 5);
  (void)descs;

  if (primeiro == 0) {
    _setText("dbStatus", "Pronto");
  } else {
    snprintf(tmp, sizeof(tmp), "E%03d", erroGetCodigo(primeiro));
    _setText("dbStatus", tmp);
  }

  snprintf(tmp, sizeof(tmp), "%u", errCount);
  _setText("dbErrCount", tmp);
  _setText("dbSerial", gSerialDock);

  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)esp_get_free_heap_size());
  _setText("dbHeap", tmp);

  snprintf(tmp, sizeof(tmp), "%d", (int)uxTaskGetNumberOfTasks());
  _setText("dbTasks", tmp);

  uint32_t s = millis() / 1000;
  snprintf(tmp, sizeof(tmp), "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600),
           (unsigned long)((s % 3600) / 60),
           (unsigned long)(s % 60));
  _setText("dbUptime", tmp);

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (gTag.presente || gTag.cacheValido) {
      _setText("dbPenId", gTag.cache.id);
      _setText("dbPenStatus", _statusTexto(gTag.cache.status).c_str());
      snprintf(tmp, sizeof(tmp), "%u", gTag.cache.ciclos);
      _setText("dbPenCycles", tmp);
      snprintf(tmp, sizeof(tmp), "%u", gTag.cache.vida);
      _setText("dbPenLife", tmp);
      _setText("dbPenSerial", gTag.cache.serial);
    } else {
      _setText("dbPenId", "N/A");
      _setText("dbPenStatus", "Sem tag");
      _setText("dbPenCycles", "--");
      _setText("dbPenLife", "--");
      _setText("dbPenSerial", "--");
    }
    xSemaphoreGive(mutexTag);
  }

  if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
    const char* txtIds[3] = { "dbCh0", "dbCh1", "dbCh2" };
    const char* barIds[3] = { "jCh0", "jCh1", "jCh2" };

    for (uint8_t ch = 0; ch < 3; ch++) {
      if (!gNivel[ch].leituraOk) {
        _setText(txtIds[ch], "OFF");
        _setValue(barIds[ch], 0);
        continue;
      }

      const uint32_t nivel = (uint32_t)_nxClamp(gNivel[ch].nivelPct, 0.0f, 100.0f);
      snprintf(tmp, sizeof(tmp), "%lu%%", (unsigned long)nivel);
      _setText(txtIds[ch], tmp);
      _setValue(barIds[ch], nivel);
    }
    xSemaphoreGive(mutexNivel);
  }

  if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(20)) == pdTRUE) {
    _setText("dbRechargeState", _rechargeStatusTexto(gRecharge.status));
    snprintf(tmp, sizeof(tmp), "CH%u", (unsigned)gRecharge.channel + 1);
    _setText("dbRechargeChannel", tmp);
    snprintf(tmp, sizeof(tmp), "%.0f%%", gRecharge.levelPct);
    _setText("dbRechargeLevel", tmp);
    snprintf(tmp, sizeof(tmp), "%u%%", gRecharge.dutyPct);
    _setText("dbRechargeDuty", tmp);
    _setValue("jRecharge", gRecharge.dutyPct);
    xSemaphoreGive(mutexRecharge);
  }
}

// =========================
// ENVIA IMAGEM HOME
// =========================
static void _verificarHome() {
  bool tcaOk = false;
  // Timeout 400 ms: taskSensor pode segurar mutexI2C ate 300 ms (AD7747)
  if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(400)) == pdTRUE) {
    Wire.beginTransmission(TCA_ADDR);
    tcaOk = (Wire.endTransmission() == 0);
    xSemaphoreGive(mutexI2C);
  }

  bool nfcOk = false;
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
    nfcOk = gTag.nfcOk;
    xSemaphoreGive(mutexTag);
  }

  if (tcaOk && nfcOk) {
    _nextionCmd("p0.pic=4");
  }
}

// =========================
// VERIFICACAO DE HARDWARE
// =========================
static bool _verificarHardware() {
  bool tcaOk = false;
  // Timeout 400 ms: taskSensor pode segurar mutexI2C ate 300 ms (AD7747)
  if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(400)) == pdTRUE) {
    Wire.beginTransmission(TCA_ADDR);
    tcaOk = (Wire.endTransmission() == 0);
    xSemaphoreGive(mutexI2C);
  }

  bool nfcOk = false;
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
    nfcOk = gTag.nfcOk;
    xSemaphoreGive(mutexTag);
  }

  return (tcaOk && nfcOk);
}

// =========================
// PROCESSA TOUCH EVENT 0x65
// Despacha ActCmd para taskAtuadores via qActCmd
// =========================
static void _processarTouch(uint8_t page, uint8_t compID, uint8_t event) {
  if (page != NEXTION_PAGE_CONFIGS || event != 0x01) return;

  ActCmd cmd;
  switch (compID) {
    case NEXTION_ID_BTN_PURGAR: cmd.type = ActCmd::ACT_PURGAR;    break;
    case NEXTION_ID_BTN_VAL1:   cmd.type = ActCmd::ACT_VALVULA_1; break;
    case NEXTION_ID_BTN_VAL2:   cmd.type = ActCmd::ACT_VALVULA_2; break;
    case NEXTION_ID_BTN_VAL3:   cmd.type = ActCmd::ACT_VALVULA_3; break;
    default: return;
  }

  xQueueSend(qActCmd, &cmd, 0);
  Serial.printf("[Nextion] Touch page=%d comp=%d -> ActCmd=%d\n",
                page, compID, (int)cmd.type);
  logdbPublishf("Nextion", "Touch", LOG_INFO, "Touch page=%u comp=%u", (unsigned)page, (unsigned)compID);
}

// =========================
// TASK NEXTION
// Core 1, prioridade 2
// =========================
void taskNextion(void *param) {
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
  vTaskDelay(pdMS_TO_TICKS(800));

  // Wire ja inicializado em setup() — nao reinicializar aqui
  if (_verificarHardware()) {
    _nextionCmd("page dock_status");
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  _nextionLimpar();

  uint32_t ultimoRefresh = 0;
  TagEvent ev;

  for (;;) {
    // ── Refresh periodico a cada 2s ───────────────────────
    if (millis() - ultimoRefresh >= 2000) {
      ultimoRefresh = millis();

      bool hwOk = _verificarHardware();

      if (hwOk) {
        _nextionCmd("p0.pic=4");
      }

      _enviarDadosDock();
      _enviarErros();
      _enviarDashboard();
    }

    // ── Le bytes do Nextion: touch events (binario) e texto ──
    {
      static char    rxBuf[32];
      static uint8_t rxIdx   = 0;
      static uint8_t ffCount = 0;

      // Maquina de estados para pacote binario 0x65
      static uint8_t touchBuf[7];
      static uint8_t touchIdx = 0;
      static bool    emTouch  = false;

      while (Serial2.available()) {
        uint8_t c = (uint8_t)Serial2.read();

        // Descarta bytes de status conhecidos que nao sao pacotes uteis
        if (!emTouch && c == 0x1A) continue;

        // Inicio de touch event
        if (!emTouch && c == 0x65) {
          emTouch      = true;
          touchBuf[0]  = 0x65;
          touchIdx     = 1;
          rxIdx        = 0;
          ffCount      = 0;
          continue;
        }

        // Acumulando touch event: 0x65 page comp event 0xFF 0xFF 0xFF
        if (emTouch) {
          touchBuf[touchIdx++] = c;
          if (touchIdx == 7) {
            emTouch = false;
            if (touchBuf[4] == 0xFF && touchBuf[5] == 0xFF && touchBuf[6] == 0xFF) {
              _processarTouch(touchBuf[1], touchBuf[2], touchBuf[3]);
            }
            touchIdx = 0;
          }
          continue;
        }

        // Texto terminado por 0xFF 0xFF 0xFF (ex: "HOME:ABRIU")
        if (c == 0xFF) {
          ffCount++;
          if (ffCount >= 3) {
            rxBuf[rxIdx] = '\0';
            String cmd = String(rxBuf);
            rxIdx   = 0;
            ffCount = 0;
            if (cmd == "HOME:ABRIU") _verificarHome();
          }
        } else {
          ffCount = 0;
          if (c >= 0x20 && rxIdx < (sizeof(rxBuf) - 1)) {
            rxBuf[rxIdx++] = (char)c;
          } else {
            rxIdx = 0;
          }
        }
      }
    }

    // ── Eventos de tag ────────────────────────────────────
    if (xQueueReceive(qNextionData, &ev, pdMS_TO_TICKS(500)) == pdTRUE) {
      switch (ev.type) {
        case TagEvent::TAG_PRESENTE:
        case TagEvent::TAG_LIDA:
        case TagEvent::TAG_GRAVADA:
        case TagEvent::TAG_RESETADA:
          _nextionMostrar(ev.data);
          break;

        case TagEvent::TAG_REMOVIDA:
          _nextionLimpar();
          break;

        case TagEvent::TAG_ERRO:
          _setText("StatusPen1", "Erro");
          break;

        default:
          break;
      }
    }
  }
}
