#pragma once
// =============================================================
//  task_nfc.h — V5
//  PN532 via SPI
//  SCK=D18  MISO=D19  MOSI=D23  CS=D13
//  Roda no Core 0, prioridade 3
// =============================================================
#include "shared.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

// ── Pinos SPI ─────────────────────────────────────────────────
#define NFC_SCK   18
#define NFC_MISO  19
#define NFC_MOSI  23
#define NFC_CS    13

// Instância SPI (CS, SCK, MISO, MOSI)
static Adafruit_PN532 nfc(NFC_CS, &SPI);

// =========================
// CONSTANTES
// =========================
static const uint32_t POLL_INTERVAL_MS = 150;
static const uint32_t TAG_TIMEOUT_MS   = 700;

// =========================
// HELPERS NTAG
// Layout:
//   page 4     → vida(lo/hi), ciclos(lo/hi)
//   page 5     → status
//   page 6-9   → serial (16 bytes)
//   page 10-13 → id (16 bytes)
// =========================
static bool _readPage(uint8_t page, uint8_t *out4) {
  uint8_t buf[32];
  if (!nfc.ntag2xx_ReadPage(page, buf)) return false;
  memcpy(out4, buf, 4);
  return true;
}

static bool _writePage(uint8_t page, const uint8_t *in4) {
  return nfc.ntag2xx_WritePage(page, (uint8_t*)in4);
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

  bool ehResposta = (type == TagEvent::TAG_LIDA    ||
                     type == TagEvent::TAG_GRAVADA  ||
                     type == TagEvent::TAG_RESETADA ||
                     type == TagEvent::TAG_ERRO);

  xQueueOverwrite(qTagData, &ev);
  if (ehResposta) xQueueOverwrite(qSerialResp, &ev);
}

// =========================
// TASK NFC — Core 0, prioridade 3
// =========================
void taskNFC(void *param) {
  // Inicia SPI com os pinos corretos
  SPI.begin(NFC_SCK, NFC_MISO, NFC_MOSI);

  pinMode(NFC_CS, OUTPUT);
  digitalWrite(NFC_CS, HIGH);

  // ── Verifica TCA9548A via I2C ──────────────────────────
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.beginTransmission(TCA_ADDR);
  uint8_t tcaErr = Wire.endTransmission();
  if (tcaErr == 0) {
    Serial.println("[NFC] TCA9548A (0x70): ONLINE");
  } else {
    Serial.printf("[NFC] TCA9548A (0x70): OFFLINE (err=%d)\n", tcaErr);
    Serial.println("[NFC] Verifique SDA=D21 SCL=D22 e pull-ups 4.7k");
  }

  nfc.begin();

  uint32_t fw = nfc.getFirmwareVersion();
  if (!fw) {
    Serial.println("[NFC] ERRO: PN532 nao encontrado via SPI.");
    Serial.println("[NFC] Verifique: SCK=D18 MISO=D19 MOSI=D23 CS=D13");
    while (true) {
      digitalWrite(LED2, !digitalRead(LED2));
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }

  Serial.printf("[NFC] PN532 OK — FW: %d.%d\n",
                (fw >> 16) & 0xFF, (fw >> 8) & 0xFF);

  nfc.SAMConfig();
  Serial.println("[NFC] Pronto. Aguardando tag...");

  uint8_t  lastUid[7] = {0};
  uint8_t  lastUidLen = 0;
  bool     tagPresente    = false;
  uint32_t ultimoDetectMs = 0;
  uint32_t ultimoPollMs   = 0;

  for (;;) {
    uint32_t now = millis();

    // ── Comandos da taskSerial ─────────────────────────────
    SerialCmd cmd;
    if (xQueueReceive(qSerialCmd, &cmd, 0) == pdTRUE) {
      if (tagPresente) {
        switch (cmd.type) {
          case SerialCmd::CMD_LER: {
            TagData d;
            if (_lerTag(d)) _publicarEvento(TagEvent::TAG_LIDA, &d);
            else            _publicarEvento(TagEvent::TAG_ERRO);
            break;
          }
          case SerialCmd::CMD_GRAVAR: {
            if (_gravarTag(cmd.payload)) {
              TagData lido;
              if (_lerTag(lido)) _publicarEvento(TagEvent::TAG_GRAVADA, &lido);
              else               _publicarEvento(TagEvent::TAG_GRAVADA, &cmd.payload);
            } else {
              _publicarEvento(TagEvent::TAG_ERRO);
            }
            break;
          }
          case SerialCmd::CMD_RESETAR: {
            if (_resetarTag()) {
              TagData lido;
              if (_lerTag(lido)) _publicarEvento(TagEvent::TAG_RESETADA, &lido);
              else               _publicarEvento(TagEvent::TAG_RESETADA);
            } else {
              _publicarEvento(TagEvent::TAG_ERRO);
            }
            break;
          }
        }
        // Fix V3: reset timers após comando para evitar race condition
        ultimoPollMs = millis();
        lastUidLen   = 0;
      }
    }

    // ── Throttle de polling ────────────────────────────────
    if (now - ultimoPollMs < POLL_INTERVAL_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    ultimoPollMs = now;

    // ── Detecta tag ───────────────────────────────────────
    uint8_t uid[7];
    uint8_t uidLen = 0;
    bool detectou = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);

    if (detectou) {
      ultimoDetectMs = now;
      bool mesmaTag  = (uidLen == lastUidLen && memcmp(uid, lastUid, uidLen) == 0);

      if (!tagPresente || !mesmaTag) {
        memcpy(lastUid, uid, uidLen);
        lastUidLen  = uidLen;
        tagPresente = true;
        digitalWrite(LED2, HIGH);
        xTaskNotify(hTaskLED, 1, eSetValueWithOverwrite);

        TagData d;
        bool ok = _lerTag(d);
        _publicarEvento(TagEvent::TAG_PRESENTE, ok ? &d : nullptr, uid, uidLen);

        Serial.print("[NFC] Tag UID: ");
        for (uint8_t i = 0; i < uidLen; i++) {
          if (uid[i] < 0x10) Serial.print("0");
          Serial.print(uid[i], HEX); Serial.print(" ");
        }
        Serial.println();
      }
    } else {
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