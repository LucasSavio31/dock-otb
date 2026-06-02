#pragma once
// =============================================================
//  task_nextion.h — V6
//
//  Mapeamento verificado contra IHM_OTB.HMI:
//
//  page status_pen / status_cart / status_dock / home:
//    Pen1 : IdPen1  tStatusPen1  tCiclosPen1  tVidaPen1  tSerialPen1
//    Pen2 : tIdPen2 tStatusPen2  tCiclosPen2  tVidaPen2  tSerialPen2
//    Pen3 : tIdPen3 tStatusPen3  tCiclosPen3  tVidaPen3  tSerialPen3
//    Cart1: tIdCart1 tStatusCart1 tCiclosCart1 tVidaCart1 tSerialCart1
//    Cart2: tIdCart2 tStatusCart2 tCiclosCart2 tVidaCart2 tSerialCart2
//    Cart3: tIdCart3 tStatusCart3 tCiclosCart3 tVidaCart3 tSerialCart3
//
//  page dock_status / menu_adm:
//    Freq HeapLiv HeapMin HeapTot Sram Flash SketTam SketLiv
//    Chip Nucleos Wifi HallSens Tasks Uptime tSerialDock
//
//  page home:
//    Uptime1 tStatus
//
//  page erros:
//    tErroCod1..5  tErroTotal  tStatus
//    btn_purgar (comp ID 6, pagina configs=4)
//
//  page recarga / andam_rec:
//    barraVermelho(CH0) barraAzul(CH1) barraAmarelo(CH2)
//    tRecarga
//
//  page configs (pg4):
//    hDuty  tDuty  btn_purgar(6) btn_val1(10) btn_val2(11) btn_val3(12)
//    Slider hDuty envia texto "PWM=<val>" ao soltar
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
// HELPERS DE CAMPO POR ÍNDICE
// Retorna o nome do campo no Nextion para cada leitor (0-5)
// 0-2 = canetas, 3-5 = cartuchos
// =========================
static const char* _nxFieldId(uint8_t ri) {
  static const char* ids[] = {
    "IdPen1",  "tIdPen2",  "tIdPen3",
    "tIdCart1","tIdCart2", "tIdCart3"
  };
  return (ri < 6) ? ids[ri] : nullptr;
}
static const char* _nxFieldStatus(uint8_t ri) {
  static const char* f[] = {
    "tStatusPen1",  "tStatusPen2",  "tStatusPen3",
    "tStatusCart1", "tStatusCart2", "tStatusCart3"
  };
  return (ri < 6) ? f[ri] : nullptr;
}
static const char* _nxFieldCiclos(uint8_t ri) {
  static const char* f[] = {
    "tCiclosPen1",  "tCiclosPen2",  "tCiclosPen3",
    "tCiclosCart1", "tCiclosCart2", "tCiclosCart3"
  };
  return (ri < 6) ? f[ri] : nullptr;
}
static const char* _nxFieldVida(uint8_t ri) {
  static const char* f[] = {
    "tVidaPen1",  "tVidaPen2",  "tVidaPen3",
    "tVidaCart1", "tVidaCart2", "tVidaCart3"
  };
  return (ri < 6) ? f[ri] : nullptr;
}
static const char* _nxFieldSerial(uint8_t ri) {
  static const char* f[] = {
    "tSerialPen1",  "tSerialPen2",  "tSerialPen3",
    "tSerialCart1", "tSerialCart2", "tSerialCart3"
  };
  return (ri < 6) ? f[ri] : nullptr;
}

// =========================
// MOSTRA / LIMPA TAG DE UM LEITOR
// =========================
static void _nextionLimparReader(uint8_t ri) {
  if (ri >= 6) return;
  _setText(_nxFieldId(ri),     "N/A");
  _setText(_nxFieldStatus(ri), "N/A");
  _setText(_nxFieldCiclos(ri), "N/A");
  _setText(_nxFieldVida(ri),   "N/A");
  _setText(_nxFieldSerial(ri), "N/A");
}

static void _nextionMostrarReader(uint8_t ri, const TagData &d) {
  if (ri >= 6) return;
  _setText(_nxFieldId(ri),     d.id);
  _setText(_nxFieldStatus(ri), _statusTexto(d.status).c_str());
  _setText(_nxFieldCiclos(ri), String(d.ciclos).c_str());
  _setText(_nxFieldVida(ri),   String(d.vida).c_str());
  _setText(_nxFieldSerial(ri), d.serial);
}

// Limpar todos os 6 leitores (chamado no boot)
static void _nextionLimparTodos() {
  for (uint8_t i = 0; i < 6; i++) _nextionLimparReader(i);
}

// Alias para compatibilidade com evento qNextionData (usa leitor ativo)
static void _nextionLimpar() {
  _nextionLimparReader(nfcCanalAtivo);
}

static void _nextionMostrar(const TagData &d) {
  _nextionMostrarReader(nfcCanalAtivo, d);
}

// =========================
// DADOS DO SISTEMA — dock_status / menu_adm
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
  nx("Uptime",  tmp);
  nx("Uptime1", tmp);  // página home

  nx("tSerialDock", gSerialDock);
}

// =========================
// DADOS DE ERROS — página erros
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

  for (uint8_t i = 0; i < 5; i++) {
    if (i < count) {
      snprintf(tmp, sizeof(tmp), "E%03d", codigos[i]);
      nx(cf[i], tmp);
    } else {
      nx(cf[i], "----");
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
// SENSORES DE NÍVEL — página recarga / andam_rec
// barraVermelho=CH0, barraAzul=CH1, barraAmarelo=CH2
// =========================
static void _enviarNiveis() {
  static const char* barras[3] = { "barraVermelho", "barraAzul", "barraAmarelo" };

  if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (uint8_t ch = 0; ch < 3; ch++) {
      uint32_t pct = 0;
      if (gNivel[ch].leituraOk) {
        pct = (uint32_t)_nxClamp(gNivel[ch].nivelPct, 0.0f, 100.0f);
      }
      _setValue(barras[ch], pct);
    }
    xSemaphoreGive(mutexNivel);
  }
}

// =========================
// ESTADO DA RECARGA — página recarga
// =========================
static void _enviarRecarga() {
  char tmp[64];
  if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(20)) == pdTRUE) {
    _setText("tRecarga", _rechargeStatusTexto(gRecharge.status));
    snprintf(tmp, sizeof(tmp), "%u%%", gRecharge.dutyPct);
    _setText("tDuty", tmp);
    _setValue("hDuty", gRecharge.dutyPct);
    xSemaphoreGive(mutexRecharge);
  }
  // Sync slider de bomba com duty atual
  _setValue("hDuty", (uint32_t)gBombaDuty);
}

// =========================
// TODOS OS LEITORES — atualiza pens e cartuchos
// =========================
static void _enviarTodosLeitores() {
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    for (uint8_t ri = 0; ri < 6; ri++) {
      if (gTagReaders[ri].presente && gTagReaders[ri].valid) {
        xSemaphoreGive(mutexTag);
        _nextionMostrarReader(ri, gTagReaders[ri].data);
        xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30));
      } else {
        xSemaphoreGive(mutexTag);
        _nextionLimparReader(ri);
        xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30));
      }
    }
    xSemaphoreGive(mutexTag);
  }
}

// =========================
// VERIFICAÇÃO DE HARDWARE
// =========================
static bool _verificarHardware() {
  bool tcaOk = false;
  if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(50)) == pdTRUE) {
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
// =========================
static void _processarTouch(uint8_t page, uint8_t compID, uint8_t event) {
  if (event != 0x01) return;
  if (page != NEXTION_PAGE_CONFIGS) return;

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
// PROCESSA COMANDO DE TEXTO (ex: "PWM=75")
// =========================
static void _processarCmdTexto(const char* cmd) {
  if (strncmp(cmd, "PWM=", 4) == 0) {
    int val = atoi(cmd + 4);
    if (val < 0)   val = 0;
    if (val > 100) val = 100;
    ControleCmd cc;
    cc.type    = ControleCmd::BOMBA_DUTY;
    cc.payload = (uint8_t)val;
    xQueueSend(qControleCmd, &cc, 0);
    Serial.printf("[Nextion] PWM slider -> %d%%\n", val);
    logdbPublishf("Nextion", "PWM", LOG_INFO, "Slider PWM=%d%%", val);
    return;
  }
  if (strcmp(cmd, "HOME:ABRIU") == 0) {
    bool tcaOk = false;
    if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(50)) == pdTRUE) {
      Wire.beginTransmission(TCA_ADDR);
      tcaOk = (Wire.endTransmission() == 0);
      xSemaphoreGive(mutexI2C);
    }
    bool nfcOk = false;
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
      nfcOk = gTag.nfcOk;
      xSemaphoreGive(mutexTag);
    }
    if (tcaOk && nfcOk) _nextionCmd("p0.pic=4");
    return;
  }
}

// =========================
// TASK NEXTION
// Core 1, prioridade 2
// =========================
void taskNextion(void *param) {
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
  vTaskDelay(pdMS_TO_TICKS(800));

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (_verificarHardware()) {
    _nextionCmd("page dock_status");
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  _nextionLimparTodos();

  uint32_t ultimoRefresh = 0;
  TagEvent ev;

  for (;;) {
    // ── Refresh periódico a cada 2s ──────────────────────────
    if (millis() - ultimoRefresh >= 2000) {
      ultimoRefresh = millis();

      if (_verificarHardware()) {
        _nextionCmd("p0.pic=4");
      }

      _enviarDadosDock();
      _enviarErros();
      _enviarNiveis();
      _enviarRecarga();
      _enviarTodosLeitores();
    }

    // ── Leitura serial do Nextion ────────────────────────────
    {
      static char    rxBuf[32];
      static uint8_t rxIdx   = 0;
      static uint8_t ffCount = 0;

      static uint8_t touchBuf[7];
      static uint8_t touchIdx = 0;
      static bool    emTouch  = false;

      while (Serial2.available()) {
        uint8_t c = (uint8_t)Serial2.read();

        if (!emTouch && c == 0x1A) continue;

        if (!emTouch && c == 0x65) {
          emTouch      = true;
          touchBuf[0]  = 0x65;
          touchIdx     = 1;
          rxIdx        = 0;
          ffCount      = 0;
          continue;
        }

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

        // Texto terminado por 0xFF 0xFF 0xFF
        if (c == 0xFF) {
          ffCount++;
          if (ffCount >= 3) {
            rxBuf[rxIdx] = '\0';
            if (rxIdx > 0) _processarCmdTexto(rxBuf);
            rxIdx   = 0;
            ffCount = 0;
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

    // ── Eventos de tag ────────────────────────────────────────
    if (xQueueReceive(qNextionData, &ev, pdMS_TO_TICKS(500)) == pdTRUE) {
      uint8_t ri = nfcCanalAtivo;
      switch (ev.type) {
        case TagEvent::TAG_PRESENTE:
        case TagEvent::TAG_LIDA:
        case TagEvent::TAG_GRAVADA:
        case TagEvent::TAG_RESETADA:
          _nextionMostrarReader(ri, ev.data);
          break;

        case TagEvent::TAG_REMOVIDA:
          _nextionLimparReader(ri);
          break;

        case TagEvent::TAG_ERRO:
          if (_nxFieldStatus(ri)) _setText(_nxFieldStatus(ri), "Erro");
          break;

        default:
          break;
      }
    }
  }
}
