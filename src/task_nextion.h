#pragma once
// =============================================================
//  task_nextion.h — V5
//  Paginas HMI:
//    dock_status     — tela principal: Uptime, tVersao, Tasks, HallSens,
//                      barraVermelho/Azul/Amarelo, RecargasHj, tRecarga,
//                      tDuty, tStatus, p0
//    status_dock     — info sistema: Freq, HeapLiv, HeapMin, HeapTot,
//                      Sram, Flash, SketTam, SketLiv, Chip, Nucleos,
//                      Wifi, HallSens, Tasks, Uptime, Serie
//    status_pen      — IdPen1-3, StatusPen1-3, CiclosPen1-3, VidaPen1-3,
//                      SerialPen1-3
//    status_cart     — IdCart1-3, StatusCart1-3, CiclosCart1-3,
//                      VidaCart1-3, SerialCart1-3
//    erros           — tErroCod1-5, tErroTotal, tStatus
//                      Botoes: btn_purgar(6), btn_val1(10), btn_val2(11),
//                              btn_val3(12)  — pagina 4
//    testes_manuais  — valvulas, sensores RAW, purga, PWM bomba
//                      Slider hDuty envia "PWM=XX\xFF\xFF\xFF" via serial
//                      Botoes m0/m1/m2 togglem valvulas 1/2/3
//                      Botao m3 aciona purga
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include "shared.h"
#include "task_erros.h"
#include "esp_chip_info.h"
#include "esp_system.h"

// =========================
// IDs DA PÁGINA com botoes de controle (pagina erros, id=4)
// =========================
#define NEXTION_PAGE_CONTROLS   4
#define NEXTION_ID_BTN_PURGAR   6
#define NEXTION_ID_BTN_VAL1    10
#define NEXTION_ID_BTN_VAL2    11
#define NEXTION_ID_BTN_VAL3    12

// =========================
// TELA TESTES MANUAIS
// Numero da pagina: verificar no monitor serial ([Nextion] Page=XX).
// O firmware descobre via sendme; ajuste NEXTION_PAGE_TESTES_MANUAIS
// se o numero nao bater com o que o monitor mostra.
//
// IDs dos componentes (ordem sequencial no Nextion Editor):
//   hDuty(3) slider PWM — envia "PWM=XX\xFF\xFF\xFF" ao soltar
//   m0(6)   hotspot valvula 1
//   m1(9)   hotspot valvula 2
//   m2(10)  hotspot valvula 3
//   m3(21)  hotspot purga
//   t0(11)  sensor CH0 raw
//   t1(12)  sensor CH1 raw
//   t2(13)  sensor CH2 raw
//   t3(18)  sensor CH0 pF
//   t4(19)  sensor CH1 pF
//   t11(17) sensor CH2 pF
//   tDuty(4) duty% atual
// =========================
#define NEXTION_PAGE_TESTES_MANUAIS  14   // ajuste se necessario
#define NEXTION_PAGE_ANAM_REC_NX      NEXTION_PAGE_ANAM_REC  // alias local = 7
#define NX_TM_M0    6   // toggle valvula 1
#define NX_TM_M1    9   // toggle valvula 2
#define NX_TM_M2   10   // toggle valvula 3
#define NX_TM_M3   21   // acionar purga

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

// Estado de tracking de pagina e valvulas (testes_manuais)
static uint8_t _pageAtual       = 0xFF;
static bool    _val1Aberta      = false;
static bool    _val2Aberta      = false;
static bool    _val3Aberta      = false;
static bool    _rechargeAtivo   = false;  // controla transições de tela de recarga

// ── Dock_status: componentes por slot de cartucho ─────────────────────────
// readers NFC 3/4/5 = cartuchos Vermelho/Azul/Verde (gTagReaders[c+3])
static const char* _dsPic[]   = { "p2",  "p3",  "p4"  };
static const int   _dsPicOk[] = {  8,     6,     5    };   // pic quando presente
static const char* _dsLA[]    = { "t2",  "t6",  "t10" };   // label A (vis)
static const char* _dsLB[]    = { "t3",  "t7",  "t11" };   // label B (vis)
static const char* _dsSer[]   = { "t5",  "t8",  "t12" };   // serial
static const char* _dsCic[]   = { "t4",  "t9",  "t13" };   // ciclos
static const char* _dsLvl[]   = { "j1",  "j2",  "j3"  };   // barra de nivel

static uint8_t _dockPageId = 0xFF;  // ID da pagina dock_status (aprendido via sendme)
static bool    _t14Blink   = false; // true = t14 pisca (todos os cartuchos presentes)
static bool    _t14Vis     = true;  // estado atual da visibilidade de t14

// =========================
// LIMPA / MOSTRA TAG — por leitor
// leitores 0-2 = canetas (IdPen1-3)
// leitores 3-5 = cartuchos (IdCart1-3)
// =========================
static const char* _penIdComp[]     = {"IdPen1",     "IdPen2",     "IdPen3"    };
static const char* _penStComp[]     = {"StatusPen1", "StatusPen2", "StatusPen3"};
static const char* _penCiComp[]     = {"CiclosPen1", "CiclosPen2", "CiclosPen3"};
static const char* _penViComp[]     = {"VidaPen1",   "VidaPen2",   "VidaPen3"  };
static const char* _penSeComp[]     = {"SerialPen1", "SerialPen2", "SerialPen3"};

static const char* _cartIdComp[]    = {"IdCart1",     "IdCart2",     "IdCart3"    };
static const char* _cartStComp[]    = {"StatusCart1", "StatusCart2", "StatusCart3"};
static const char* _cartCiComp[]    = {"CiclosCart1", "CiclosCart2", "CiclosCart3"};
static const char* _cartViComp[]    = {"VidaCart1",   "VidaCart2",   "VidaCart3"  };
static const char* _cartSeComp[]    = {"SerialCart1", "SerialCart2", "SerialCart3"};

static void _nextionLimparReader(uint8_t readerIdx) {
  if (readerIdx < 3) {
    _setText(_penIdComp[readerIdx], "N/A");
    _setText(_penStComp[readerIdx], "N/A");
    _setText(_penCiComp[readerIdx], "--");
    _setText(_penViComp[readerIdx], "--");
    _setText(_penSeComp[readerIdx], "--");
  } else if (readerIdx < 6) {
    uint8_t c = readerIdx - 3;
    _setText(_cartIdComp[c], "N/A");
    _setText(_cartStComp[c], "N/A");
    _setText(_cartCiComp[c], "--");
    _setText(_cartViComp[c], "--");
    _setText(_cartSeComp[c], "--");
  }
}

static void _nextionMostrarReader(uint8_t readerIdx, const TagData &d) {
  char tmp[32];
  if (readerIdx < 3) {
    _setText(_penIdComp[readerIdx], d.id);
    _setText(_penStComp[readerIdx], _statusTexto(d.status).c_str());
    snprintf(tmp, sizeof(tmp), "%u", d.ciclos);
    _setText(_penCiComp[readerIdx], tmp);
    snprintf(tmp, sizeof(tmp), "%u", d.vida);
    _setText(_penViComp[readerIdx], tmp);
    _setText(_penSeComp[readerIdx], d.serial);
  } else if (readerIdx < 6) {
    uint8_t c = readerIdx - 3;
    _setText(_cartIdComp[c], d.id);
    _setText(_cartStComp[c], _statusTexto(d.status).c_str());
    snprintf(tmp, sizeof(tmp), "%u", d.ciclos);
    _setText(_cartCiComp[c], tmp);
    snprintf(tmp, sizeof(tmp), "%u", d.vida);
    _setText(_cartViComp[c], tmp);
    _setText(_cartSeComp[c], d.serial);
  }
}

static void _nextionLimparTodos() {
  for (uint8_t i = 0; i < 6; i++) _nextionLimparReader(i);
}

// Compatibilidade com código legado que usa reader 0
static void _nextionLimpar()                   { _nextionLimparReader(0); }
static void _nextionMostrar(const TagData &d)  { _nextionMostrarReader(0, d); }

// =========================
// TELA PRINCIPAL — dock_status
// Atualiza os componentes reais da tela principal do HMI
// =========================
static void _enviarDockScreen() {
  char tmp[64];
  uint32_t s = millis() / 1000;

  // Uptime
  snprintf(tmp, sizeof(tmp), "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600),
           (unsigned long)((s % 3600) / 60),
           (unsigned long)(s % 60));
  _setText("Uptime", tmp);

  // Sistema
  snprintf(tmp, sizeof(tmp), "%d", (int)uxTaskGetNumberOfTasks());
  _setText("Tasks", tmp);
  snprintf(tmp, sizeof(tmp), "%d", hallRead());
  _setText("HallSens", tmp);

  // Niveis dos cartuchos (barras de progresso 0-100)
  _setValue("barraVermelho", (uint32_t)gCartLevel[1]);
  _setValue("barraAzul",     (uint32_t)gCartLevel[2]);
  _setValue("barraAmarelo",  (uint32_t)gCartLevel[3]);

  // Contador de recargas hoje
  snprintf(tmp, sizeof(tmp), "%u", (unsigned)gRechargeCount);
  _setText("RecargasHj", tmp);

  // Estado da recarga e duty da bomba
  if (xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(20)) == pdTRUE) {
    _setText("tRecarga", _rechargeStatusTexto(gRecharge.status));
    snprintf(tmp, sizeof(tmp), "%u%%", (unsigned)gRecharge.dutyPct);
    _setText("tDuty", tmp);
    xSemaphoreGive(mutexRecharge);
  }

  // Status / erro ativo
  uint8_t primeiro = erroGetPrimeiro();
  if (primeiro == 0) {
    _setText("tStatus", "Pronto");
  } else {
    snprintf(tmp, sizeof(tmp), "E%03d", erroGetCodigo(primeiro));
    _setText("tStatus", tmp);
  }
}

// =========================
// DADOS DO SISTEMA — status_dock
// Atualiza a pagina de info detalhada do sistema
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
  nx("tVersao", FIRMWARE_VERSION);
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

  // tStatus: indicador de erro visivel em todas as paginas que o contem
  uint8_t primeiro = erroGetPrimeiro();
  if (primeiro == 0) {
    nx("tStatus", "Pronto");
  } else {
    snprintf(tmp, sizeof(tmp), "E%03d", erroGetCodigo(primeiro));
    nx("tStatus", tmp);
  }

  // Codigos na pagina erros
  const char* cf[] = {"tErroCod1", "tErroCod2", "tErroCod3", "tErroCod4", "tErroCod5"};
  for (uint8_t i = 0; i < 5; i++) {
    if (i < count) {
      snprintf(tmp, sizeof(tmp), "E%03d", codigos[i]);
      nx(cf[i], tmp);
    } else {
      nx(cf[i], "----");
    }
  }

  // Total
  if (count == 0) {
    nx("tErroTotal", "Nenhum erro ativo");
  } else {
    snprintf(tmp, sizeof(tmp), "%d erro(s) ativo(s)", count);
    nx("tErroTotal", tmp);
  }
}

// =========================
// ATUALIZA TODOS OS LEITORES (chamado no refresh periodico)
// =========================
static void _atualizarLeitores() {
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    for (uint8_t i = 0; i < 6; i++) {
      if (gTagReaders[i].presente && gTagReaders[i].valid) {
        _nextionMostrarReader(i, gTagReaders[i].data);
      } else {
        _nextionLimparReader(i);
      }
    }
    xSemaphoreGive(mutexTag);
  }
}

// =========================
// ENVIA IMAGEM HOME (resposta ao evento HOME:ABRIU)
// =========================
static void _verificarHome() {
  bool tcaOk = false;
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
// TESTES MANUAIS — atualiza sensores RAW e duty na tela
// Chamado apenas quando _pageAtual == NEXTION_PAGE_TESTES_MANUAIS
// =========================
static void _enviarTestesManuais() {
  char tmp[32];

  // Leituras dos sensores por canal (t0=CH0 raw, t1=CH1 raw, t2=CH2 raw)
  if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (uint8_t ch = 0; ch < 3; ch++) {
      snprintf(tmp, sizeof(tmp), "%ld", (long)gNivel[ch].rawAtual);
      const char* txComp[] = {"t0", "t1", "t2"};
      _setText(txComp[ch], tmp);

      // t3=CH0 pF, t4=CH1 pF, t11=CH2 pF
      snprintf(tmp, sizeof(tmp), "%.3fpF", gNivel[ch].pFAtual);
      const char* tfComp[] = {"t3", "t4", "t11"};
      _setText(tfComp[ch], tmp);
    }
    xSemaphoreGive(mutexNivel);
  }

  // Duty atual da bomba
  snprintf(tmp, sizeof(tmp), "%u%%", (unsigned)gBombaDuty);
  _setText("tDuty", tmp);
}

// =========================
// DOCK_STATUS — atualiza slot de cartucho individual
// c=0/1/2 (Vermelho/Azul/Verde)
// presente = cartucho detectado pelo NFC
// valid    = dados NTAG lidos com sucesso
// =========================
static void _dockSlot(uint8_t c, bool presente, bool valid, const TagData &d) {
  char cmd[32];
  // Imagem: troca para presente assim que detectado (nao depende de valid)
  snprintf(cmd, sizeof(cmd), "%s.pic=%d", _dsPic[c], presente ? _dsPicOk[c] : 7);
  _nextionCmd(cmd);

  if (presente) {
    snprintf(cmd, sizeof(cmd), "vis %s,1", _dsLA[c]);  _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,1", _dsLB[c]);  _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,1", _dsLvl[c]); _nextionCmd(cmd);

    if (valid) {
      // Dados NFC disponiveis: mostra serial (t5/t8/t12) e ciclos (t4/t9/t13)
      snprintf(cmd, sizeof(cmd), "vis %s,1", _dsSer[c]); _nextionCmd(cmd);
      snprintf(cmd, sizeof(cmd), "vis %s,1", _dsCic[c]); _nextionCmd(cmd);
      _setText(_dsSer[c], d.serial[0] ? d.serial : "---");
      char tmp[12];
      snprintf(tmp, sizeof(tmp), "%u", (unsigned)d.ciclos);
      _setText(_dsCic[c], tmp);
    } else {
      // Leitura NFC pendente: esconde serial e ciclos ate dados chegarem
      snprintf(cmd, sizeof(cmd), "vis %s,0", _dsSer[c]); _nextionCmd(cmd);
      snprintf(cmd, sizeof(cmd), "vis %s,0", _dsCic[c]); _nextionCmd(cmd);
    }

    // Barra de nivel: 100% - ciclos*5 (5% por recarga, baseado na tag NFC — persiste reboot).
    uint8_t lvl = 100u;
    if (valid) {
      uint32_t usado = (uint32_t)d.ciclos * 5u;
      lvl = (usado >= 100u) ? 0u : (uint8_t)(100u - usado);
    }
    _setValue(_dsLvl[c], (uint32_t)lvl);
  } else {
    snprintf(cmd, sizeof(cmd), "vis %s,0", _dsLA[c]);  _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,0", _dsLB[c]);  _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,0", _dsLvl[c]); _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,0", _dsSer[c]); _nextionCmd(cmd);
    snprintf(cmd, sizeof(cmd), "vis %s,0", _dsCic[c]); _nextionCmd(cmd);
    _setValue(_dsLvl[c], 0);
  }
}

// =========================
// DOCK_STATUS — atualiza tela principal em tempo real
// Cartuchos, t14 status/blink, t15 versão, t16 uptime
// Guard interno: só executa quando na pagina dock_status
// =========================
static void _dockStatusAtualizar() {
  // Guarda de pagina: so executa quando o ID de dock_status foi aprendido
  // e a pagina ativa e exatamente essa.
  if (_dockPageId == 0xFF || _pageAtual != _dockPageId) return;

  // Snapshot dos cartuchos (readers 3-5) sob mutexTag
  bool    presente[3] = {};
  bool    valid[3]    = {};
  TagData data[3]     = {};
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(30)) == pdTRUE) {
    for (uint8_t c = 0; c < 3; c++) {
      presente[c] = gTagReaders[c + 3].presente;
      valid[c]    = gTagReaders[c + 3].valid;
      data[c]     = gTagReaders[c + 3].data;
    }
    xSemaphoreGive(mutexTag);
  } else return;

  static const char* nomesCart[] = { "VERMELHO", "AZUL", "VERDE" };
  bool allOk    = true;
  char t14Msg[48] = "";

  for (uint8_t c = 0; c < 3; c++) {
    bool ok = presente[c] && valid[c];
    _dockSlot(c, presente[c], valid[c], data[c]);
    if (!ok && t14Msg[0] == '\0') {
      snprintf(t14Msg, sizeof(t14Msg), "CART. %s AUSENTE", nomesCart[c]);
      allOk = false;
    }
  }

  // Erro ativo tem prioridade sobre mensagem de cartucho ausente
  uint8_t priErr = erroGetPrimeiro();
  if (priErr != 0) {
    allOk = false;
    snprintf(t14Msg, sizeof(t14Msg), "ERRO E%03d ATIVO", erroGetCodigo(priErr));
  }

  // t14: status geral
  if (allOk) {
    if (!_t14Blink) {
      _setText("t14", "INSIRA A CANETA PARA RECARGA!");
      _nextionCmd("vis t14,1");
      _t14Vis = true;
    }
    _t14Blink = true;
  } else {
    _t14Blink = false;
    _setText("t14", t14Msg);
    _nextionCmd("vis t14,1");
    _t14Vis = true;
  }

  // t15: versao do firmware
  _setText("t15", FIRMWARE_VERSION);

  // t16: uptime hh:mm:ss
  {
    char ut[12];
    uint32_t s = millis() / 1000;
    snprintf(ut, sizeof(ut), "%02lu:%02lu:%02lu",
             (unsigned long)(s / 3600),
             (unsigned long)((s % 3600) / 60),
             (unsigned long)(s % 60));
    _setText("t16", ut);
  }
}

// =========================
// PROCESSA TOUCH EVENT 0x65
// Pagina NEXTION_PAGE_CONTROLS: botoes erros/controle
// Pagina NEXTION_PAGE_TESTES_MANUAIS: valvulas, purga
// =========================
static void _processarTouch(uint8_t page, uint8_t compID, uint8_t event) {
  if (event != 0x01) return;

  // ── Pagina anam_rec (tela 7): botão de emergência m0 ────
  if (page == NEXTION_PAGE_ANAM_REC) {
    // compID 0 = m0 (botão de parada de emergência)
    RechargeCmd sc{};
    sc.type = RechargeCmd::STOP;
    xQueueSend(qRechargeCmd, &sc, 0);
    Serial.println("[Nextion] EMERGENCIA: recarga interrompida via m0 na tela 7");
    logdbPublishf("Nextion", "Emergencia", LOG_WARN, "Parada emergencia recarga (m0 p7)");
    return;
  }

  // ── Pagina erros/controle ────────────────────────────────
  if (page == NEXTION_PAGE_CONTROLS) {
    ActCmd cmd;
    switch (compID) {
      case NEXTION_ID_BTN_PURGAR: cmd.type = ActCmd::ACT_PURGAR;    break;
      case NEXTION_ID_BTN_VAL1:   cmd.type = ActCmd::ACT_VALVULA_1; break;
      case NEXTION_ID_BTN_VAL2:   cmd.type = ActCmd::ACT_VALVULA_2; break;
      case NEXTION_ID_BTN_VAL3:   cmd.type = ActCmd::ACT_VALVULA_3; break;
      default: return;
    }
    xQueueSend(qActCmd, &cmd, 0);
    Serial.printf("[Nextion] Touch controls page=%d comp=%d -> ActCmd=%d\n",
                  page, compID, (int)cmd.type);
    logdbPublishf("Nextion", "Touch", LOG_INFO, "Controls p=%u c=%u", (unsigned)page, (unsigned)compID);
    return;
  }

  // ── Pagina testes_manuais ────────────────────────────────
  if (page == NEXTION_PAGE_TESTES_MANUAIS) {
    ControleCmd cc;
    switch (compID) {
      case NX_TM_M0:
        _val1Aberta = !_val1Aberta;
        cc.type    = _val1Aberta ? ControleCmd::VALVULA_ON : ControleCmd::VALVULA_OFF;
        cc.payload = 1;
        xQueueSend(qControleCmd, &cc, 0);
        Serial.printf("[Nextion] Valvula1 %s\n", _val1Aberta ? "ABERTA" : "FECHADA");
        break;
      case NX_TM_M1:
        _val2Aberta = !_val2Aberta;
        cc.type    = _val2Aberta ? ControleCmd::VALVULA_ON : ControleCmd::VALVULA_OFF;
        cc.payload = 2;
        xQueueSend(qControleCmd, &cc, 0);
        Serial.printf("[Nextion] Valvula2 %s\n", _val2Aberta ? "ABERTA" : "FECHADA");
        break;
      case NX_TM_M2:
        _val3Aberta = !_val3Aberta;
        cc.type    = _val3Aberta ? ControleCmd::VALVULA_ON : ControleCmd::VALVULA_OFF;
        cc.payload = 3;
        xQueueSend(qControleCmd, &cc, 0);
        Serial.printf("[Nextion] Valvula3 %s\n", _val3Aberta ? "ABERTA" : "FECHADA");
        break;
      case NX_TM_M3: {
        ActCmd ac;
        ac.type = ActCmd::ACT_PURGAR;
        xQueueSend(qActCmd, &ac, 0);
        Serial.println("[Nextion] Purga acionada");
        break;
      }
      default: break;
    }
    logdbPublishf("Nextion", "Touch", LOG_INFO, "Testes p=%u c=%u", (unsigned)page, (unsigned)compID);
  }
}

// =========================
// TELA 7 — anam_rec
// Preenche os campos da tela de recarga em andamento.
// Chamado uma vez ao iniciar a recarga e não durante o ciclo.
// =========================
static void _preencherAnamRec(const RechargeInfo &ri) {
  char buf[32];
  // t6 = ID da caneta
  _setText("t6", ri.penId[0] ? ri.penId : "---");
  // t0 = posição (1/2/3)
  snprintf(buf, sizeof(buf), "%u", (unsigned)(ri.channel + 1));
  _setText("t0", buf);
  // t1 = leitor (mesmo índice da posição para canetas 1/2/3)
  _setText("t1", buf);
  // t2 = serial da caneta
  _setText("t2", ri.penSerial[0] ? ri.penSerial : "---");
  // t3 = ciclos da caneta
  snprintf(buf, sizeof(buf), "%u", ri.penCiclos);
  _setText("t3", buf);
  // t4 = ID do cartucho
  _setText("t4", ri.cartId[0] ? ri.cartId : "---");
  // t5 = serial do cartucho
  _setText("t5", ri.cartSerial[0] ? ri.cartSerial : "---");
  // j0 = nível atual (0-100%)
  _setValue("j0", (uint32_t)_nxClamp(ri.levelPct, 0.0f, 100.0f));
}

// =========================
// TASK NEXTION
// Core 1, prioridade 2
// =========================
void taskNextion(void *param) {
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Força página 1 (dock_status) por número — cobre o reboot pós-unlock onde
  // o Nextion retém a página 15 (bloqueio) enquanto o ESP32 ainda não enviou comandos.
  _nextionCmd("page 1");
  vTaskDelay(pdMS_TO_TICKS(300));

  // Limpa todos os slots de canetas e cartuchos
  _nextionLimparTodos();

  // Dispara sendme para capturar ID da pagina dock_status.
  // _dockStatusAtualizar() sera chamado pelo handler do evento 0x66
  // quando o ID for aprendido, evitando enviar pic=7 em outras telas.
  _nextionCmd("sendme");
  vTaskDelay(pdMS_TO_TICKS(150));
  _verificarHome();

  uint32_t ultimoRefresh  = 0;
  uint32_t ultimoSendme   = 0;
  uint32_t ultimoBlink    = 0;
  TagEvent ev;

  for (;;) {
    // ── Bloqueio por violação: mantém página 15 e inibe tudo ──
    if (gBloqueado) {
      _nextionCmd("page 15");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // ── Refresh periodico a cada 2s ───────────────────────
    if (millis() - ultimoRefresh >= 2000) {
      ultimoRefresh = millis();

      // ── Gerencia transições de tela de recarga (anam_rec) ───
      RechargeInfo ri = {};
      if (mutexRecharge && xSemaphoreTake(mutexRecharge, pdMS_TO_TICKS(10)) == pdTRUE) {
        ri = gRecharge;
        xSemaphoreGive(mutexRecharge);
      }
      bool rechargeAtivo = (ri.status == RechargeInfo::RUNNING ||
                            ri.status == RechargeInfo::TAPERING);

      if (rechargeAtivo && !_rechargeAtivo) {
        // Recarga iniciou → navega para tela 7 e preenche campos
        _nextionCmd("page 7");
        vTaskDelay(pdMS_TO_TICKS(150));
        _preencherAnamRec(ri);
      } else if (!rechargeAtivo && _rechargeAtivo) {
        // Recarga terminou → volta para dock_status
        vTaskDelay(pdMS_TO_TICKS(1500));
        _nextionCmd("page 1");
        vTaskDelay(pdMS_TO_TICKS(200));
        _nextionCmd("sendme");
      } else if (rechargeAtivo && _pageAtual == NEXTION_PAGE_ANAM_REC) {
        // Atualiza j0 com o nível atual durante a recarga
        _setValue("j0", (uint32_t)_nxClamp(ri.levelPct, 0.0f, 100.0f));
      }
      _rechargeAtivo = rechargeAtivo;

      // ── Refresh periódico das demais telas ──────────────────
      bool naDock = (_dockPageId != 0xFF && _pageAtual == _dockPageId);
      if (_pageAtual == NEXTION_PAGE_ANAM_REC) {
        // Tela 7 gerenciada acima; não sobrescreve
      } else if (naDock) {
        _enviarDockScreen();
        _dockStatusAtualizar();
      } else if (_pageAtual == NEXTION_PAGE_TESTES_MANUAIS) {
        _enviarTestesManuais();
      } else {
        _enviarDadosDock();
        _enviarErros();
        _atualizarLeitores();
      }
    }

    // ── sendme a cada 5s para rastrear pagina atual ───────
    if (millis() - ultimoSendme >= 5000) {
      ultimoSendme = millis();
      _nextionCmd("sendme");
    }

    // ── Blink de t14 a cada 600ms quando todos os cartuchos OK ───────────
    if (_t14Blink && (millis() - ultimoBlink >= 600)) {
      ultimoBlink = millis();
      if (_dockPageId == 0xFF || _pageAtual == _dockPageId) {
        _t14Vis = !_t14Vis;
        _nextionCmd(_t14Vis ? "vis t14,1" : "vis t14,0");
      }
    }

    // ── Le bytes do Nextion ───────────────────────────────
    {
      static char    rxBuf[64];
      static uint8_t rxIdx   = 0;
      static uint8_t ffCount = 0;

      // Parser de pacotes binarios (touch 0x65, page 0x66)
      static uint8_t pktBuf[8];
      static uint8_t pktIdx   = 0;
      static uint8_t pktLen   = 0;   // comprimento esperado do pacote
      static bool    emPkt    = false;

      while (Serial2.available()) {
        uint8_t c = (uint8_t)Serial2.read();

        // Ignora byte de keep-alive 0x1A
        if (!emPkt && c == 0x1A) continue;

        // Inicio de pacote binario
        if (!emPkt && (c == 0x65 || c == 0x66)) {
          emPkt      = true;
          pktBuf[0]  = c;
          pktLen     = (c == 0x65) ? 7 : 5;  // touch=7 bytes, page=5 bytes
          pktIdx     = 1;
          rxIdx      = 0;
          ffCount    = 0;
          continue;
        }

        if (emPkt) {
          pktBuf[pktIdx++] = c;
          if (pktIdx >= pktLen) {
            emPkt = false;
            // Verifica terminador 0xFF 0xFF 0xFF
            bool ok = (pktBuf[pktLen-3] == 0xFF &&
                       pktBuf[pktLen-2] == 0xFF &&
                       pktBuf[pktLen-1] == 0xFF);
            if (ok) {
              if (pktBuf[0] == 0x65) {
                // Touch event: [0x65][page][comp][event][FF FF FF]
                _processarTouch(pktBuf[1], pktBuf[2], pktBuf[3]);
              } else if (pktBuf[0] == 0x66) {
                // Page event: [0x66][page_id][FF FF FF]
                uint8_t pid = pktBuf[1];
                // Primeira resposta 0x66 apos boot = pagina dock_status
                if (_dockPageId == 0xFF) {
                  _dockPageId = pid;
                  Serial.printf("[Nextion] dock_status pageId=%u\n", (unsigned)pid);
                }
                if (pid != _pageAtual) {
                  Serial.printf("[Nextion] Page=%u\n", (unsigned)pid);
                  // Ao entrar em testes_manuais, reseta estado das valvulas
                  if (pid == NEXTION_PAGE_TESTES_MANUAIS) {
                    _val1Aberta = false;
                    _val2Aberta = false;
                    _val3Aberta = false;
                  }
                  _pageAtual = pid;
                  // Postinitialize: ao voltar para dock_status atualiza imediatamente
                  if (pid == _dockPageId) {
                    _verificarHome();
                    _dockStatusAtualizar();
                  }
                }
              }
            }
            pktIdx = 0;
            pktLen = 0;
          }
          continue;
        }

        // Texto terminado por 0xFF 0xFF 0xFF
        if (c == 0xFF) {
          ffCount++;
          if (ffCount >= 3) {
            rxBuf[rxIdx] = '\0';
            String msg = String(rxBuf);
            rxIdx   = 0;
            ffCount = 0;

            if (msg == "HOME:ABRIU") {
              _verificarHome();
            } else if (msg.startsWith("PWM=")) {
              // Slider hDuty em testes_manuais enviou: PWM=XX
              int duty = msg.substring(4).toInt();
              if (duty < 0)   duty = 0;
              if (duty > 100) duty = 100;
              ControleCmd cc;
              cc.type    = ControleCmd::BOMBA_DUTY;
              cc.payload = (uint8_t)duty;
              xQueueSend(qControleCmd, &cc, 0);
              Serial.printf("[Nextion] PWM slider=%d%%\n", duty);
              logdbPublishf("Nextion", "PWM", LOG_INFO, "duty=%d", duty);
            }
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

    // ── Eventos de tag (queue taskNFC → taskNextion) ─────
    if (xQueueReceive(qNextionData, &ev, pdMS_TO_TICKS(500)) == pdTRUE) {
      bool naDock = (_dockPageId != 0xFF && _pageAtual == _dockPageId);
      switch (ev.type) {
        case TagEvent::TAG_PRESENTE:
        case TagEvent::TAG_LIDA:
        case TagEvent::TAG_GRAVADA:
        case TagEvent::TAG_RESETADA:
          if (!naDock) _nextionMostrarReader(ev.readerIdx, ev.data);
          _dockStatusAtualizar();
          break;

        case TagEvent::TAG_REMOVIDA:
          if (!naDock) _nextionLimparReader(ev.readerIdx);
          _dockStatusAtualizar();
          break;

        case TagEvent::TAG_ERRO:
          if (!naDock) {
            if (ev.readerIdx < 3) {
              _setText(_penStComp[ev.readerIdx], "Erro");
            } else if (ev.readerIdx < 6) {
              _setText(_cartStComp[ev.readerIdx - 3], "Erro");
            }
          }
          _dockStatusAtualizar();
          break;

        default:
          break;
      }
    }
  }
}
