#pragma once
// =============================================================
//  task_nfc.h - V5
//  PN532 via SPI | SCK=D18 MISO=D19 MOSI=D23 CS=D13
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

// Mapeamento 6 leitores
static const uint8_t NFC_CS_PINS[6] = {13, 14, 4, 5, 15, 2};
static const char*   NFC_NOMES[6]   = {
  "Caneta 1",   "Caneta 2",   "Caneta 3",
  "Cartucho 1", "Cartucho 2", "Cartucho 3"
};

// Indices ERR correspondentes a cada leitor (posicao no array gErros)
static const uint8_t NFC_ERR_IDX[6] = {
  ERR_E101, ERR_E102, ERR_E103,
  ERR_E104, ERR_E105, ERR_E106
};

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

static bool _selecionarLeitor(uint8_t readerIdx) {
  if (readerIdx >= 6) return false;
  if (!(nfcReaderOkMask & (1 << readerIdx))) return false;

  if (nfcCanalAtivo != readerIdx) {
    for (uint8_t i = 0; i < 6; i++) digitalWrite(NFC_CS_PINS[i], HIGH);
    nfc = nfcReaders[readerIdx];
    nfc->begin();
    nfc->SAMConfig();
    nfcCanalAtivo = readerIdx;
    _limparEstadoTag();
    Serial.printf("[NFC] Leitor ativo: %s (CS=D%d)\n", NFC_NOMES[readerIdx], NFC_CS_PINS[readerIdx]);
  }

  return true;
}

static bool _detectarUid(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs = 80) {
  *uidLen = 0;
  return nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, timeoutMs);
}

// =========================
// HELPERS NTAG
// =========================
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

// =========================
// PUBLICA evento
// =========================
static void _publicarEvento(TagEvent::Type type, const TagData *data = nullptr,
                            const uint8_t *uid = nullptr, uint8_t uidLen = 0) {
  TagEvent ev{};
  ev.type = type;
  if (data) ev.data = *data;
  if (uid)  { memcpy(ev.uid, uid, uidLen); ev.uidLen = uidLen; }

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
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
      logdbPublishf("NFC", "Tag", LOG_INFO, "Tag detectada no leitor ativo.");
      break;
    case TagEvent::TAG_REMOVIDA:
      logdbPublish("NFC", "Tag", LOG_WARN, "Tag removida do leitor ativo.");
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

// =========================
// VERIFICA todos os 6 leitores no boot
// Seta/limpa ERR_E101-E106 individualmente
// Retorna bitmask dos leitores OK (bit 0 = leitor 0, etc.)
// =========================
static uint8_t _verificarLeitores() {
  uint8_t okMask = 0;

  // Garante todos CS em HIGH antes de comecar
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(NFC_CS_PINS[i], OUTPUT);
    digitalWrite(NFC_CS_PINS[i], HIGH);
  }

  for (uint8_t i = 0; i < 6; i++) {
    Adafruit_PN532 nfcTmp(NFC_CS_PINS[i], &SPI);
    nfcTmp.begin();
    vTaskDelay(pdMS_TO_TICKS(20));
    uint32_t fw = nfcTmp.getFirmwareVersion();

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

    // Devolve CS ao estado inativo
    digitalWrite(NFC_CS_PINS[i], HIGH);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  return okMask;
}

// =========================
// TASK NFC
// =========================
void taskNFC(void *param) {
  SPI.begin(NFC_SCK, NFC_MISO, NFC_MOSI);

  // Verifica TCA9548A
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.beginTransmission(TCA_ADDR);
  uint8_t tcaErr = Wire.endTransmission();
  if (tcaErr == 0) erroClear(ERR_E201);
  else             erroSetar(ERR_E201);

  // -- Verifica todos os 6 leitores ----------------------
  Serial.println("[NFC] Verificando leitores...");
  uint8_t okMask = _verificarLeitores();
  nfcReaderOkMask = okMask;

  // Leitor 0 (CS=D13) e o principal - sem ele nao ha operacao
  if (!(okMask & 0x01)) {
    Serial.println("[NFC] ERRO CRITICO: Leitor principal (D13) ausente.");
    while (true) { digitalWrite(LED2, !digitalRead(LED2)); vTaskDelay(pdMS_TO_TICKS(200)); }
  }

  // -- Inicializa leitor principal para operacao ----------
  nfc->begin();
  nfc->SAMConfig();

  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(100)) == pdTRUE) {
    gTag.nfcOk = true;
    xSemaphoreGive(mutexTag);
  }

  Serial.printf("[NFC] Pronto. Leitores OK: %d/6\n", __builtin_popcount(okMask));

  uint8_t  lastUid[7]     = {0};
  uint8_t  lastUidLen     = 0;
  bool     tagPresente    = false;
  uint32_t ultimoDetectMs = 0;
  uint32_t ultimoPollMs   = 0;

  for (;;) {
    uint32_t now = millis();

    // -- Reinit apos _menuDiag ter resetado os chips via SPI --
    if (nfcReinitPending) {
      nfcReinitPending = false;
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        nfc->begin();
        nfc->SAMConfig();
        xSemaphoreGive(mutexSPI);
      }
      lastUidLen = 0;
      tagPresente = false;
      Serial.println("[NFC] Reinit apos diagnostico.");
    }

    // -- Comandos da taskSerial -----------------------------
    SerialCmd cmd;
    if (xQueueReceive(qSerialCmd, &cmd, 0) == pdTRUE) {
      if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        uint8_t oldReader = nfcCanalAtivo;
        bool leitorOk = _selecionarLeitor(cmd.readerIdx);
        if (leitorOk && oldReader != cmd.readerIdx) {
          tagPresente = false;
          lastUidLen = 0;
        }

        uint8_t uid[7];
        uint8_t uidLen = 0;
        bool detectou = leitorOk && _detectarUid(uid, &uidLen, 120);

        if (!leitorOk || !detectou) {
          tagPresente = false;
          lastUidLen = 0;
          _limparEstadoTag();
          xSemaphoreGive(mutexSPI);
          _publicarEvento(TagEvent::TAG_ERRO);
        } else {
          ultimoDetectMs = millis();
          memcpy(lastUid, uid, uidLen);
          lastUidLen = uidLen;
          tagPresente = true;

          switch (cmd.type) {
            case SerialCmd::CMD_IDENTIFICAR:
              xSemaphoreGive(mutexSPI);
              _publicarEvento(TagEvent::TAG_LIDA, nullptr, uid, uidLen);
              xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              break;

            case SerialCmd::CMD_LER: {
              TagData d;
              if (_lerTag(d)) {
                erroClear(ERR_E110);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_LIDA, &d, uid, uidLen);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                erroSetar(ERR_E110);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO);
              }
              break;
            }

            case SerialCmd::CMD_GRAVAR: {
              if (_gravarTag(cmd.payload)) {
                TagData lido;
                bool relido = _lerTag(lido);
                erroClear(ERR_E111);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_GRAVADA, relido ? &lido : &cmd.payload, uid, uidLen);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                erroSetar(ERR_E111);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO);
              }
              break;
            }

            case SerialCmd::CMD_RESETAR: {
              if (_resetarTag()) {
                TagData lido;
                bool relido = _lerTag(lido);
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_RESETADA, relido ? &lido : nullptr, uid, uidLen);
                xTaskNotify(hTaskLED, 2, eSetValueWithOverwrite);
              } else {
                xSemaphoreGive(mutexSPI);
                _publicarEvento(TagEvent::TAG_ERRO);
              }
              break;
            }

            default:
              xSemaphoreGive(mutexSPI);
              break;
          }
        }
      }
      ultimoPollMs = millis();
    }

    // -- Throttle de polling --------------------------------
    if (now - ultimoPollMs < POLL_INTERVAL_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    ultimoPollMs = now;

    // -- Detecta tag ---------------------------------------
    if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(100)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t uid[7];
    uint8_t uidLen = 0;
    bool detectou = _detectarUid(uid, &uidLen, 50);

    if (detectou) {
      ultimoDetectMs = now;
      bool mesmaTag  = (uidLen == lastUidLen && memcmp(uid, lastUid, uidLen) == 0);

      if (!tagPresente || !mesmaTag) {
        memcpy(lastUid, uid, uidLen);
        lastUidLen  = uidLen;
        tagPresente = true;

        TagData d;
        bool ok = false;
        for (uint8_t t = 0; t < 3 && !ok; t++) {
          if (t > 0) vTaskDelay(pdMS_TO_TICKS(50));
          ok = _lerTag(d);
        }
        xSemaphoreGive(mutexSPI);

        digitalWrite(LED2, HIGH);
        xTaskNotify(hTaskLED, 1, eSetValueWithOverwrite);
        _publicarEvento(TagEvent::TAG_PRESENTE, ok ? &d : nullptr, uid, uidLen);

        Serial.print("[NFC] Tag detectada. UID: ");
        for (uint8_t i = 0; i < uidLen; i++) {
          if (uid[i] < 0x10) Serial.print("0");
          Serial.print(uid[i], HEX); Serial.print(" ");
        }
        Serial.println();
      } else {
        xSemaphoreGive(mutexSPI);
      }
    } else {
      xSemaphoreGive(mutexSPI);
      if (tagPresente && (now - ultimoDetectMs > TAG_TIMEOUT_MS)) {
        tagPresente = false;
        memset(lastUid, 0, sizeof(lastUid));
        lastUidLen  = 0;
        digitalWrite(LED2, LOW);
        xTaskNotify(hTaskLED, 0, eSetValueWithOverwrite);
        _publicarEvento(TagEvent::TAG_REMOVIDA);
        Serial.println("[NFC] Tag removida.");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
