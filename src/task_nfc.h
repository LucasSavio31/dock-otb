#pragma once
// =============================================================
//  task_nfc.h - V5
//  PN532 via SPI | SCK=D18 MISO=D19 MOSI=D23
//  3 leitores de caneta: D13 (Caneta1) D14 (Caneta2) D4 (Caneta3)
//  Round-robin polling — NAO acessa barramento I2C durante operacao
//  Wire.begin() feito em setup() — nenhuma competicao com taskSensor
//  Core 0, prioridade 3
// =============================================================
#include "shared.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#define NFC_SCK   18
#define NFC_MISO  19
#define NFC_MOSI  23
#define NFC_CS    13

// Mapeamento dos 6 leitores (indices 0-5)
static const uint8_t NFC_CS_PINS[6] = {13, 14, 4, 5, 15, 32};
static const char*   NFC_NOMES[6]   = {
  "Caneta 1",   "Caneta 2",   "Caneta 3",
  "Cartucho 1", "Cartucho 2", "Cartucho 3"
};
static const uint8_t NFC_ERR_IDX[6] = {
  ERR_E101, ERR_E102, ERR_E103,
  ERR_E104, ERR_E105, ERR_E106
};

// Canetas 0-2 + Cartuchos 3-5 (D5/D15/D32). D2 substituido por D32.
#define NFC_POLL_READERS 6

static Adafruit_PN532 nfcReader1(NFC_CS_PINS[0], &SPI);
static Adafruit_PN532 nfcReader2(NFC_CS_PINS[1], &SPI);
static Adafruit_PN532 nfcReader3(NFC_CS_PINS[2], &SPI);
static Adafruit_PN532 nfcReader4(NFC_CS_PINS[3], &SPI);
static Adafruit_PN532 nfcReader5(NFC_CS_PINS[4], &SPI);
static Adafruit_PN532 nfcReader6(NFC_CS_PINS[5], &SPI);
static Adafruit_PN532 *nfcReaders[6] = {
  &nfcReader1, &nfcReader2, &nfcReader3,
  &nfcReader4, &nfcReader5, &nfcReader6
};
static Adafruit_PN532 *nfc = nfcReaders[0];

static const uint32_t POLL_INTERVAL_MS = 150;
static const uint32_t TAG_TIMEOUT_MS   = 700;

// Estado independente por leitor (round-robin, todos os 6)
static bool     _tagPresente[NFC_POLL_READERS]  = {};
static bool     _dataOk[NFC_POLL_READERS]       = {};  // leitura NTAG bem-sucedida
static uint8_t  _lastUid[NFC_POLL_READERS][7]   = {};
static uint8_t  _lastUidLen[NFC_POLL_READERS]   = {};
static uint32_t _ultDetectMs[NFC_POLL_READERS]  = {};
static uint8_t  _pollReader = 0;

// =============================================================
// HELPERS
// =============================================================

// Atualiza o LED com base no estado atual das tags e saude do hardware.
// Banco 1 = canetas  (readers 0-2, D13/D14/D4)   mask 0x07
// Banco 2 = cartuchos (readers 3-4, D5/D15)       mask 0x18  (D2 excluido)
// Regra:
//   - Qualquer tag presente                                    → LED on (1)
//   - Sem tag, banco 2 completo (bits 3+4) OU banco 1 OK (>=1) → LED off (0)
//   - Sem tag, nenhum reader de nenhum banco detectado          → LED pisca (2)
static void _ledAtualizar() {
  if (!hTaskLED) return;
  bool anyTag = false;
  for (uint8_t i = 0; i < NFC_POLL_READERS; i++) {
    if (_tagPresente[i]) { anyTag = true; break; }
  }
  if (anyTag) {
    xTaskNotify(hTaskLED, 1, eSetValueWithOverwrite);
  } else if (nfcReaderOkMask == 0) {
    xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
  } else {
    xTaskNotify(hTaskLED, 0, eSetValueWithOverwrite);
  }
}

static void _limparEstadoTag() {
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
    gTag.presente    = false;
    gTag.cacheValido = false;
    gTag.uidLen      = 0;
    memset(gTag.uid, 0, sizeof(gTag.uid));
    memset(&gTag.cache, 0, sizeof(gTag.cache));
    xSemaphoreGive(mutexTag);
  }
}

// Troca CS sem reinicializar chip — poll round-robin (apenas GPIO, sem SPI)
static void _switchReader(uint8_t readerIdx) {
  if (nfcCanalAtivo == readerIdx) return;
  for (uint8_t i = 0; i < 6; i++) {
    // D2 (LED2) nao e deselected aqui — a biblioteca PN532 gerencia o CS dele.
    // Evita toggle acidental do LED onboard durante o round-robin.
    if (NFC_CS_PINS[i] == LED2) continue;
    digitalWrite(NFC_CS_PINS[i], HIGH);
  }
  nfc           = nfcReaders[readerIdx];
  nfcCanalAtivo = readerIdx;
}

// Troca leitor com reinit completo (begin + SAMConfig) — para comandos de taskSerial
static bool _selecionarLeitor(uint8_t readerIdx) {
  if (readerIdx >= 6) return false;
  if (!(nfcReaderOkMask & (1 << readerIdx))) return false;

  if (nfcCanalAtivo != readerIdx) {
    for (uint8_t i = 0; i < 6; i++) {
      if (NFC_CS_PINS[i] == LED2) continue;
      digitalWrite(NFC_CS_PINS[i], HIGH);
    }
    nfc = nfcReaders[readerIdx];
    nfc->begin();
    vTaskDelay(pdMS_TO_TICKS(30));
    nfc->SAMConfig();
    vTaskDelay(pdMS_TO_TICKS(20));
    nfcCanalAtivo = readerIdx;
    _limparEstadoTag();
    Serial.printf("[NFC] Leitor ativo: %s (CS=D%d)\n",
                  NFC_NOMES[readerIdx], NFC_CS_PINS[readerIdx]);
  }
  return true;
}

static bool _detectarUid(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs = 80) {
  *uidLen = 0;
  return nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, timeoutMs);
}

// =============================================================
// NTAG2xx
// =============================================================
static bool _readPage(uint8_t page, uint8_t *out4) {
  uint8_t buf[32];
  if (!nfc->ntag2xx_ReadPage(page, buf)) return false;
  memcpy(out4, buf, 4);
  return true;
}

static bool _writePage(uint8_t page, const uint8_t *in4) {
  return nfc->ntag2xx_WritePage(page, (uint8_t*)in4);
}

static bool _lerTag(TagData &d) {
  uint8_t p[4];
  memset(&d, 0, sizeof(d));
  if (!_readPage(4, p)) return false;
  d.vida   = word(p[1], p[0]);
  d.ciclos = word(p[3], p[2]);
  if (!_readPage(5, p)) return false;
  d.status = p[0];
  for (int i = 0; i < 4; i++) {
    if (!_readPage(6 + i, p)) return false;
    memcpy(&d.serial[i * 4], p, 4);
  }
  d.serial[16] = '\0';
  for (int i = 0; i < 4; i++) {
    if (!_readPage(10 + i, p)) return false;
    memcpy(&d.id[i * 4], p, 4);
  }
  d.id[16] = '\0';
  return true;
}

static bool _gravarTag(const TagData &d) {
  uint8_t p[4];
  p[0] = lowByte(d.vida);   p[1] = highByte(d.vida);
  p[2] = lowByte(d.ciclos); p[3] = highByte(d.ciclos);
  if (!_writePage(4, p)) return false;
  p[0] = d.status; p[1] = p[2] = p[3] = 0;
  if (!_writePage(5, p)) return false;
  char buf[16] = {0};
  strncpy(buf, d.serial, 16);
  for (int i = 0; i < 4; i++) {
    memcpy(p, &buf[i * 4], 4);
    if (!_writePage(6 + i, p)) return false;
  }
  memset(buf, 0, 16);
  strncpy(buf, d.id, 16);
  for (int i = 0; i < 4; i++) {
    memcpy(p, &buf[i * 4], 4);
    if (!_writePage(10 + i, p)) return false;
  }
  return true;
}

static bool _resetarTag() {
  TagData d{};
  d.vida   = 100;
  d.ciclos = 0;
  d.status = 1;
  strncpy(d.id,     "OTB000", sizeof(d.id)     - 1);
  strncpy(d.serial, "---",    sizeof(d.serial) - 1);
  return _gravarTag(d);
}

// =============================================================
// EVENTO
// =============================================================
static void _publicarEvento(TagEvent::Type type, const TagData *data = nullptr,
                            const uint8_t *uid = nullptr, uint8_t uidLen = 0,
                            uint8_t readerIdx = 0) {
  TagEvent ev{};
  ev.type      = type;
  ev.readerIdx = readerIdx;
  if (data) ev.data = *data;
  if (uid)  { memcpy(ev.uid, uid, uidLen); ev.uidLen = uidLen; }

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
    // Estado global (leitor primario)
    switch (type) {
      case TagEvent::TAG_PRESENTE:
        gTag.presente    = true;
        gTag.cacheValido = (data != nullptr);
        if (data) gTag.cache = *data;
        if (uid)  { memcpy(gTag.uid, uid, uidLen); gTag.uidLen = uidLen; }
        break;
      case TagEvent::TAG_REMOVIDA:
        gTag.presente    = false;
        gTag.cacheValido = false;
        memset(&gTag.cache, 0, sizeof(gTag.cache));
        break;
      case TagEvent::TAG_LIDA:
      case TagEvent::TAG_GRAVADA:
      case TagEvent::TAG_RESETADA:
        if (data) { gTag.cache = *data; gTag.cacheValido = true; }
        break;
      default: break;
    }
    // Estado por leitor (gTagReaders[0-5])
    if (readerIdx < 6) {
      switch (type) {
        case TagEvent::TAG_PRESENTE:
          gTagReaders[readerIdx].presente = true;
          gTagReaders[readerIdx].valid    = (data != nullptr);
          if (data) gTagReaders[readerIdx].data = *data;
          break;
        case TagEvent::TAG_REMOVIDA:
          gTagReaders[readerIdx].presente = false;
          gTagReaders[readerIdx].valid    = false;
          memset(&gTagReaders[readerIdx].data, 0, sizeof(TagData));
          break;
        case TagEvent::TAG_LIDA:
        case TagEvent::TAG_GRAVADA:
        case TagEvent::TAG_RESETADA:
          if (data) { gTagReaders[readerIdx].data = *data; gTagReaders[readerIdx].valid = true; }
          break;
        case TagEvent::TAG_ERRO:
          gTagReaders[readerIdx].valid = false;
          break;
        default: break;
      }
    }
    xSemaphoreGive(mutexTag);
  }

  xQueueSend(qTagData,     &ev, pdMS_TO_TICKS(50));
  xQueueSend(qNextionData, &ev, pdMS_TO_TICKS(50));

  bool ehResposta = (type == TagEvent::TAG_LIDA    ||
                     type == TagEvent::TAG_GRAVADA  ||
                     type == TagEvent::TAG_RESETADA ||
                     type == TagEvent::TAG_ERRO);
  if (ehResposta) xQueueOverwrite(qSerialResp, &ev);

  switch (type) {
    case TagEvent::TAG_PRESENTE:
      logdbPublishf("NFC", "Tag", LOG_INFO,
                    "Tag detectada no leitor %u.", (unsigned)(readerIdx + 1));
      break;
    case TagEvent::TAG_REMOVIDA:
      logdbPublishf("NFC", "Tag", LOG_WARN,
                    "Tag removida do leitor %u.", (unsigned)(readerIdx + 1));
      break;
    case TagEvent::TAG_LIDA:
      logdbPublish("NFC", "Leitura", LOG_SUCCESS, "Leitura NFC concluida.");
      break;
    case TagEvent::TAG_GRAVADA:
      logdbPublish("NFC", "Gravacao", LOG_SUCCESS, "Tag gravada com sucesso.");
      break;
    case TagEvent::TAG_RESETADA:
      logdbPublish("NFC", "Reset", LOG_WARN, "Tag resetada para padrao.");
      break;
    case TagEvent::TAG_ERRO:
      logdbPublish("NFC", "Falha", LOG_ERROR, "Falha em operacao NFC.");
      break;
    default:
      break;
  }
}

// =============================================================
// BOOT: verifica os 6 leitores no SPI
// Retorna bitmask dos leitores OK (bit 0 = leitor 0 / D13, etc.)
// D2 substituido por D32 — sem conflito com LED onboard.
// =============================================================
static uint8_t _verificarLeitores() {
  uint8_t okMask = 0;

  // Todos CS em HIGH antes de comecar
  for (uint8_t i = 0; i < 6; i++) {
    if (NFC_CS_PINS[i] == LED2) continue;
    pinMode(NFC_CS_PINS[i], OUTPUT);
    digitalWrite(NFC_CS_PINS[i], HIGH);
  }

  for (uint8_t i = 0; i < 6; i++) {
    uint32_t fw = 0;

    // Ate 3 tentativas por leitor — D5 pode precisar de reset apos outros begin()
    for (uint8_t attempt = 0; attempt < 3 && !fw; attempt++) {
      if (attempt > 0) {
        Serial.printf("[NFC] %s (CS=D%d): tentativa %u...\n",
                      NFC_NOMES[i], NFC_CS_PINS[i], attempt + 1);
        // Re-deselect todos e aguarda antes de retry
        for (uint8_t j = 0; j < 6; j++) {
          if (NFC_CS_PINS[j] == LED2) continue;
          digitalWrite(NFC_CS_PINS[j], HIGH);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
      }

      Adafruit_PN532 nfcTmp(NFC_CS_PINS[i], &SPI);
      nfcTmp.begin();
      // Re-assert todos CS HIGH apos begin() (lib pode ter reconfigurado D5)
      for (uint8_t j = 0; j < 6; j++) {
        if (NFC_CS_PINS[j] == LED2) continue;
        digitalWrite(NFC_CS_PINS[j], HIGH);
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      fw = nfcTmp.getFirmwareVersion();
    }

    if (fw) {
      erroClear(NFC_ERR_IDX[i]);
      okMask |= (1 << i);
      Serial.printf("[NFC] %s (CS=D%d): ONLINE  FW=%d.%d\n",
                    NFC_NOMES[i], NFC_CS_PINS[i],
                    (fw >> 16) & 0xFF, (fw >> 8) & 0xFF);
    } else {
      erroSetar(NFC_ERR_IDX[i]);
      Serial.printf("[NFC] %s (CS=D%d): OFFLINE\n",
                    NFC_NOMES[i], NFC_CS_PINS[i]);
    }

    digitalWrite(NFC_CS_PINS[i], HIGH);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  return okMask;
}

// =============================================================
// TASK NFC
// Core 0, prioridade 3
// =============================================================
void taskNFC(void *param) {
  // SS=-1 impede o hardware SPI de tomar conta do GPIO5 (D5 = CS do Cartucho 1).
  // Sem isso, o VSPI aloca GPIO5 como hardware SS e conflita com o CS por software.
  SPI.begin(NFC_SCK, NFC_MISO, NFC_MOSI, -1);
  // Reclaim pinos CS como GPIO output — D2 ignorado (taskLED controla GPIO2)
  for (uint8_t i = 0; i < 6; i++) {
    if (NFC_CS_PINS[i] == LED2) continue;
    pinMode(NFC_CS_PINS[i], OUTPUT);
    digitalWrite(NFC_CS_PINS[i], HIGH);
  }
  vTaskDelay(pdMS_TO_TICKS(50));

  // Wire.begin() ja feito em setup() — taskNFC NAO acessa I2C durante operacao.
  // Unico toque em Wire e este ping de boot, protegido pelo mutex.
  if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(3500)) == pdTRUE) {
    Wire.beginTransmission(TCA_ADDR);
    uint8_t tcaErr = Wire.endTransmission();
    if (tcaErr == 0) erroClear(ERR_E201);
    else             erroSetar(ERR_E201);
    xSemaphoreGive(mutexI2C);
  }

  Serial.println("[NFC] Verificando leitores...");
  uint8_t okMask = _verificarLeitores();
  nfcReaderOkMask = okMask;

  // LED inicial: pisca se nenhum banco completo; apagado se pelo menos um OK
  _ledAtualizar();

  if (!(okMask & 0x01)) {
    Serial.println("[NFC] AVISO: Leitor principal (D13) ausente. Continuando com leitores disponiveis.");
  }

  // Inicializa todos os leitores disponiveis (canetas 0-2 e cartuchos 3-4)
  for (uint8_t r = 0; r < NFC_POLL_READERS; r++) {
    if (!(okMask & (1 << r))) continue;
    for (uint8_t i = 0; i < 6; i++) {
      if (NFC_CS_PINS[i] == LED2) continue;
      digitalWrite(NFC_CS_PINS[i], HIGH);
    }
    nfcReaders[r]->begin();
    nfcReaders[r]->SAMConfig();
    vTaskDelay(pdMS_TO_TICKS(20));
    Serial.printf("[NFC] Leitor %u (CS=D%d) pronto.\n", r + 1, NFC_CS_PINS[r]);
  }

  // Inicia pelo leitor 0
  nfc           = nfcReaders[0];
  nfcCanalAtivo = 0;
  _pollReader   = 0;

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(100)) == pdTRUE) {
    gTag.nfcOk = true;
    xSemaphoreGive(mutexTag);
  }

  Serial.printf("[NFC] Pronto. Leitores OK: %d/6\n", __builtin_popcount(okMask));

  uint32_t ultimoPollMs = 0;

  for (;;) {
    uint32_t now = millis();

    // ── Recuperação periódica de leitores offline (a cada 30s) ──
    {
      static uint32_t ultimaRecuperacaoMs = 0;
      uint8_t failMask = (~nfcReaderOkMask) & 0x3F;
      if (failMask && (now - ultimaRecuperacaoMs) >= 30000) {
        ultimaRecuperacaoMs = now;
        if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
          for (uint8_t r = 0; r < NFC_POLL_READERS; r++) {
            if (!(failMask & (1 << r))) continue;
            for (uint8_t j = 0; j < 6; j++) {
              if (NFC_CS_PINS[j] == LED2) continue;
              digitalWrite(NFC_CS_PINS[j], HIGH);
            }
            nfcReaders[r]->begin();
            for (uint8_t j = 0; j < 6; j++) {
              if (NFC_CS_PINS[j] == LED2) continue;
              digitalWrite(NFC_CS_PINS[j], HIGH);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            uint32_t fw = nfcReaders[r]->getFirmwareVersion();
            if (fw) {
              nfcReaders[r]->SAMConfig();
              vTaskDelay(pdMS_TO_TICKS(20));
              nfcReaderOkMask |= (1 << r);
              erroClear(NFC_ERR_IDX[r]);
              Serial.printf("[NFC] Leitor %u (CS=D%d) recuperado automaticamente.\n",
                            r + 1, NFC_CS_PINS[r]);
            }
            digitalWrite(NFC_CS_PINS[r], HIGH);
            vTaskDelay(pdMS_TO_TICKS(30));
          }
          xSemaphoreGive(mutexSPI);
        }
      }
    }

    // ── Reinit apos _menuDiag resetar os chips via SPI ────────
    if (nfcReinitPending) {
      nfcReinitPending = false;
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (uint8_t r = 0; r < NFC_POLL_READERS; r++) {
          if (!(nfcReaderOkMask & (1 << r))) continue;
          for (uint8_t i = 0; i < 6; i++) {
            if (NFC_CS_PINS[i] == LED2) continue;
            digitalWrite(NFC_CS_PINS[i], HIGH);
          }
          nfcReaders[r]->begin();
          nfcReaders[r]->SAMConfig();
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        _switchReader(_pollReader);
        xSemaphoreGive(mutexSPI);
      }
      for (uint8_t r = 0; r < NFC_POLL_READERS; r++) {
        _tagPresente[r] = false;
        _lastUidLen[r]  = 0;
      }
      _limparEstadoTag();
      Serial.println("[NFC] Reinit apos diagnostico.");
    }

    // ── Reinit leitores de caneta (0/1/2) após recarga concluída ─
    if (nfcPenReinitPending) {
      nfcPenReinitPending = false;
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (uint8_t r = 0; r < 3; r++) {
          if (!(nfcReaderOkMask & (1 << r))) continue;
          for (uint8_t i = 0; i < 6; i++) {
            if (NFC_CS_PINS[i] == LED2) continue;
            digitalWrite(NFC_CS_PINS[i], HIGH);
          }
          nfcReaders[r]->begin();
          nfcReaders[r]->SAMConfig();
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        _switchReader(_pollReader);
        xSemaphoreGive(mutexSPI);
      }
      // Limpa estado para forçar nova detecção no próximo ciclo de poll
      if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (uint8_t r = 0; r < 3; r++) {
          gTagReaders[r].presente = false;
          gTagReaders[r].valid    = false;
        }
        xSemaphoreGive(mutexTag);
      }
      for (uint8_t r = 0; r < 3; r++) {
        _tagPresente[r] = false;
        _lastUidLen[r]  = 0;
      }
      Serial.println("[NFC] Reinit leitores 1/2/3 apos recarga.");
    }

    // ── Comandos da taskSerial ────────────────────────────────
    SerialCmd cmd;
    if (xQueueReceive(qSerialCmd, &cmd, 0) == pdTRUE) {
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        uint8_t oldReader = nfcCanalAtivo;
        bool leitorOk = _selecionarLeitor(cmd.readerIdx);

        // Limpa estado do leitor anterior se era de poll
        if (leitorOk && oldReader != cmd.readerIdx && oldReader < NFC_POLL_READERS) {
          _tagPresente[oldReader] = false;
          _lastUidLen[oldReader]  = 0;
        }

        uint8_t uid[7];
        uint8_t uidLen = 0;
        bool detectou = leitorOk && _detectarUid(uid, &uidLen, 120);

        if (!leitorOk || !detectou) {
          xSemaphoreGive(mutexSPI);
          _publicarEvento(TagEvent::TAG_ERRO, nullptr, nullptr, 0, cmd.readerIdx);
        } else {
          switch (cmd.type) {
            case SerialCmd::CMD_IDENTIFICAR:
              xSemaphoreGive(mutexSPI);
              _publicarEvento(TagEvent::TAG_LIDA, nullptr, uid, uidLen, cmd.readerIdx);
              xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              break;

            case SerialCmd::CMD_LER: {
              TagData d;
              if (_lerTag(d)) {
                erroClear(ERR_E110);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_LIDA, &d, uid, uidLen, cmd.readerIdx);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                erroSetar(ERR_E110);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO, nullptr, nullptr, 0, cmd.readerIdx);
              }
              break;
            }

            case SerialCmd::CMD_GRAVAR: {
              if (_gravarTag(cmd.payload)) {
                TagData lido;
                bool relido = _lerTag(lido);
                erroClear(ERR_E111);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_GRAVADA, relido ? &lido : &cmd.payload, uid, uidLen, cmd.readerIdx);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                erroSetar(ERR_E111);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO, nullptr, nullptr, 0, cmd.readerIdx);
              }
              break;
            }

            case SerialCmd::CMD_RESETAR: {
              if (_resetarTag()) {
                TagData lido;
                bool relido = _lerTag(lido);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_RESETADA, relido ? &lido : nullptr, uid, uidLen, cmd.readerIdx);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO, nullptr, nullptr, 0, cmd.readerIdx);
              }
              break;
            }

            default:
              xSemaphoreGive(mutexSPI);
              break;
          }
        }

        // Restaura leitor de poll apos comando (apenas GPIO, mutex ja liberado)
        _switchReader(_pollReader);
      }
      ultimoPollMs = millis();
    }

    // ── Throttle de polling ────────────────────────────────────
    if (now - ultimoPollMs < POLL_INTERVAL_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // ── Aguarda I2C ocioso antes do poll SPI ──────────────────
    // Nao avanca ultimoPollMs: quando I2C liberar, o poll ocorre imediatamente
    if (gI2CBusy) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    ultimoPollMs = now;

    // ── Round-robin: encontra proximo leitor disponivel ────────
    uint8_t r        = _pollReader;
    bool    encontrou = false;
    for (uint8_t attempt = 0; attempt < NFC_POLL_READERS; attempt++) {
      if (nfcReaderOkMask & (1 << r)) { encontrou = true; break; }
      r = (uint8_t)((r + 1) % NFC_POLL_READERS);
    }
    if (!encontrou) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    // ── Poll do leitor r ───────────────────────────────────────
    if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(100)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    _switchReader(r);

    // Leitores de cartucho (4/5/6, r>=3) ficam a ~5 cm um do outro.
    // Liga RF apenas durante o poll para evitar que campos vizinhos se sobreponham.
    if (r >= 3) {
      uint8_t rfOn[3] = {PN532_COMMAND_RFCONFIGURATION, 0x01, 0x01};
      nfcReaders[r]->sendCommandCheckAck(rfOn, 3, 50);
      vTaskDelay(pdMS_TO_TICKS(10)); // aguarda campo estabilizar
    }

    uint8_t uid[7];
    uint8_t uidLen = 0;
    bool detectou = _detectarUid(uid, &uidLen, 50);

    if (detectou) {
      _ultDetectMs[r] = now;
      bool mesmaTag   = (_lastUidLen[r] == uidLen &&
                         memcmp(uid, _lastUid[r], uidLen) == 0);

      if (!_tagPresente[r] || !mesmaTag) {
        // Nova tag (ou tag diferente): primeira leitura
        memcpy(_lastUid[r], uid, uidLen);
        _lastUidLen[r]  = uidLen;
        _tagPresente[r] = true;

        TagData d;
        bool ok = false;
        for (uint8_t t = 0; t < 3 && !ok; t++) {
          if (t > 0) vTaskDelay(pdMS_TO_TICKS(50));
          ok = _lerTag(d);
        }
        _dataOk[r] = ok;
        xSemaphoreGive(mutexSPI);

        digitalWrite(LED2, HIGH);
        xTaskNotify(hTaskLED, 1, eSetValueWithOverwrite);
        _publicarEvento(TagEvent::TAG_PRESENTE, ok ? &d : nullptr, uid, uidLen, r);

        Serial.printf("[NFC] Tag leitor %u. UID:", r + 1);
        for (uint8_t i = 0; i < uidLen; i++) Serial.printf(" %02X", uid[i]);
        Serial.println();
      } else if (!_dataOk[r]) {
        // Mesma tag presente mas leitura NTAG falhou antes — retry
        TagData d;
        bool ok = _lerTag(d);
        if (ok) {
          _dataOk[r] = true;
          xSemaphoreGive(mutexSPI);
          _publicarEvento(TagEvent::TAG_LIDA, &d, _lastUid[r], _lastUidLen[r], r);
          Serial.printf("[NFC] Retry leitura leitor %u OK.\n", r + 1);
        } else {
          xSemaphoreGive(mutexSPI);
        }
      } else {
        xSemaphoreGive(mutexSPI);
      }
    } else {
      xSemaphoreGive(mutexSPI);
      if (_tagPresente[r] && (now - _ultDetectMs[r] > TAG_TIMEOUT_MS)) {
        _tagPresente[r] = false;
        _dataOk[r]      = false;
        memset(_lastUid[r], 0, sizeof(_lastUid[r]));
        _lastUidLen[r]  = 0;

        _ledAtualizar();

        _publicarEvento(TagEvent::TAG_REMOVIDA, nullptr, nullptr, 0, r);
        Serial.printf("[NFC] Tag removida do leitor %u.\n", r + 1);
      }
    }

    // Desliga RF do leitor de cartucho antes de avançar para o próximo
    if (r >= 3) {
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint8_t rfOff[3] = {PN532_COMMAND_RFCONFIGURATION, 0x01, 0x00};
        nfcReaders[r]->sendCommandCheckAck(rfOff, 3, 50);
        xSemaphoreGive(mutexSPI);
      }
    }

    // Avanca round-robin
    _pollReader = (uint8_t)((r + 1) % NFC_POLL_READERS);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
