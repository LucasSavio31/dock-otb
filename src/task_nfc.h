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
static const uint8_t NFC_CS_PINS[6] = {13, 14, 4, 5, 15, 2};
static const char*   NFC_NOMES[6]   = {
  "Caneta 1",   "Caneta 2",   "Caneta 3",
  "Cartucho 1", "Cartucho 2", "Cartucho 3"
};
static const uint8_t NFC_ERR_IDX[6] = {
  ERR_E101, ERR_E102, ERR_E103,
  ERR_E104, ERR_E105, ERR_E106
};

// Apenas os 3 leitores de caneta participam do round-robin de poll
#define NFC_POLL_READERS 3

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

// Estado independente por leitor de caneta (round-robin)
static bool     _tagPresente[NFC_POLL_READERS]  = {false, false, false};
static uint8_t  _lastUid[NFC_POLL_READERS][7]   = {};
static uint8_t  _lastUidLen[NFC_POLL_READERS]   = {0, 0, 0};
static uint32_t _ultDetectMs[NFC_POLL_READERS]  = {0, 0, 0};
static uint8_t  _pollReader = 0;

// =============================================================
// HELPERS
// =============================================================

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
    nfc->SAMConfig();
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
// D2 (LED2) e ignorado — nao pode ser usado como CS
// =============================================================
static uint8_t _verificarLeitores() {
  uint8_t okMask = 0;

  // Todos CS em HIGH antes de comecar, exceto LED2
  for (uint8_t i = 0; i < 6; i++) {
    if (NFC_CS_PINS[i] == LED2) continue;
    pinMode(NFC_CS_PINS[i], OUTPUT);
    digitalWrite(NFC_CS_PINS[i], HIGH);
  }

  for (uint8_t i = 0; i < 6; i++) {
    // D2 e LED — nao testa SPI, mas seta erro pois o leitor esta indisponivel
    if (NFC_CS_PINS[i] == LED2) {
      erroSetar(NFC_ERR_IDX[i]);
      Serial.printf("[NFC] %s (CS=D%d): OFFLINE (pino LED reservado)\n",
                    NFC_NOMES[i], NFC_CS_PINS[i]);
      continue;
    }

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
  SPI.begin(NFC_SCK, NFC_MISO, NFC_MOSI);

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

  if (!(okMask & 0x01)) {
    Serial.println("[NFC] ERRO CRITICO: Leitor principal (D13) ausente.");
    while (true) { digitalWrite(LED2, !digitalRead(LED2)); vTaskDelay(pdMS_TO_TICKS(200)); }
  }

  // Inicializa todos os leitores de caneta disponiveis (indices 0-2)
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

    uint8_t uid[7];
    uint8_t uidLen = 0;
    bool detectou = _detectarUid(uid, &uidLen, 50);

    if (detectou) {
      _ultDetectMs[r] = now;
      bool mesmaTag   = (_lastUidLen[r] == uidLen &&
                         memcmp(uid, _lastUid[r], uidLen) == 0);

      if (!_tagPresente[r] || !mesmaTag) {
        memcpy(_lastUid[r], uid, uidLen);
        _lastUidLen[r]  = uidLen;
        _tagPresente[r] = true;

        TagData d;
        bool ok = false;
        for (uint8_t t = 0; t < 3 && !ok; t++) {
          if (t > 0) vTaskDelay(pdMS_TO_TICKS(50));
          ok = _lerTag(d);
        }
        xSemaphoreGive(mutexSPI);

        digitalWrite(LED2, HIGH);
        xTaskNotify(hTaskLED, 1, eSetValueWithOverwrite);
        _publicarEvento(TagEvent::TAG_PRESENTE, ok ? &d : nullptr, uid, uidLen, r);

        Serial.printf("[NFC] Tag leitor %u. UID:", r + 1);
        for (uint8_t i = 0; i < uidLen; i++) Serial.printf(" %02X", uid[i]);
        Serial.println();
      } else {
        xSemaphoreGive(mutexSPI);
      }
    } else {
      xSemaphoreGive(mutexSPI);
      if (_tagPresente[r] && (now - _ultDetectMs[r] > TAG_TIMEOUT_MS)) {
        _tagPresente[r] = false;
        memset(_lastUid[r], 0, sizeof(_lastUid[r]));
        _lastUidLen[r]  = 0;

        // Apaga LED apenas se nenhum leitor tem tag
        bool algumPresente = false;
        for (uint8_t i = 0; i < NFC_POLL_READERS; i++) {
          if (_tagPresente[i]) { algumPresente = true; break; }
        }
        if (!algumPresente) {
          xTaskNotify(hTaskLED, 0, eSetValueWithOverwrite);
        }

        _publicarEvento(TagEvent::TAG_REMOVIDA, nullptr, nullptr, 0, r);
        Serial.printf("[NFC] Tag removida do leitor %u.\n", r + 1);
      }
    }

    // Avanca round-robin
    _pollReader = (uint8_t)((r + 1) % NFC_POLL_READERS);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
