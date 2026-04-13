#pragma once
// =============================================================
//  task_nextion.h — V5
//  Tela: status_pen
//  Campos: IdPen1, StatusPen1, CiclosPen1, VidaPen1, SerialPen1
//  Boot: se NFC + TCA presentes → page home + imagem ID 2
// =============================================================
#include "shared.h"

// =========================
// HELPERS NEXTION
// =========================
static void _nextionCmd(const String &cmd) {
  Serial2.print(cmd);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
  Serial2.write(0xFF);
}

static String _sanitizar(String s) {
  s.replace("\"", "'");
  return s;
}

static void _setText(const char *obj, const String &txt) {
  _nextionCmd(String("status_pen.") + obj + ".txt=\"" + _sanitizar(txt) + "\"");
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

static void _nextionLimpar() {
  _setText("IdPen1",     "N/A");
  _setText("StatusPen1", "N/A");
  _setText("CiclosPen1", "N/A");
  _setText("VidaPen1",   "N/A");
  _setText("SerialPen1", "N/A");
}

static void _nextionMostrar(const TagData &d) {
  _setText("IdPen1",     String(d.id));
  _setText("StatusPen1", _statusTexto(d.status));
  _setText("CiclosPen1", String(d.ciclos));
  _setText("VidaPen1",   String(d.vida));
  _setText("SerialPen1", String(d.serial));
}

// Verifica TCA9548A e PN532 — retorna true se ambos presentes
static bool _verificarHardware() {
  // TCA9548A
  Wire.beginTransmission(TCA_ADDR);
  bool tcaOk = (Wire.endTransmission() == 0);

  // PN532 via SPI — tenta getFirmwareVersion
  // (nfc já foi inicializado na taskNFC antes desta task rodar)
  // Usa flag do gTag para saber se NFC iniciou com sucesso
  bool nfcOk = false;
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
    nfcOk = gTag.nfcOk;
    xSemaphoreGive(mutexTag);
  }

  Serial.printf("[Nextion] TCA=%s  NFC=%s\n",
                tcaOk ? "OK" : "OFFLINE",
                nfcOk ? "OK" : "OFFLINE");
  return tcaOk && nfcOk;
}

// =========================
// TASK NEXTION
// Core 1, prioridade 2
// =========================
void taskNextion(void *param) {
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
  vTaskDelay(pdMS_TO_TICKS(800)); // aguarda Nextion e taskNFC iniciarem

  // ── Verificação de hardware no boot ──────────────────────
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (_verificarHardware()) {
    // NFC + TCA presentes → vai para home com imagem ID 2
    _nextionCmd("page dock_status");
    vTaskDelay(pdMS_TO_TICKS(300));  // aguarda tela carregar
    _nextionCmd("vaClicks.val=0");    // reseta cliques
    _nextionCmd("tmClick.en=0");      // garante timer desligado
    vTaskDelay(pdMS_TO_TICKS(50));
    _nextionCmd("p0.pic=2");          // imagem OK
    Serial.println("[Nextion] Boot OK — dock_status, vaClicks=0, tmClick=0, pic=2");
  } else {
    Serial.println("[Nextion] Boot — hardware ausente, sem navegacao");
  }

  _nextionLimpar();
  Serial.println("[Nextion] Pronto.");

  // Controla reenvio periódico da imagem
  bool     hwOk         = _verificarHardware();
  uint32_t ultimoRefresh = 0;

  TagEvent ev;
  for (;;) {
    // ── Reenvio periódico da imagem a cada 3s ──────────────
    // Garante que a imagem aparece mesmo quando o timer do
    // Nextion navega para dock_status sem passar pelo ESP32
    if (hwOk && millis() - ultimoRefresh >= 3000) {
      ultimoRefresh = millis();
      _nextionCmd("vaClicks.val=0");
      _nextionCmd("tmClick.en=0");
      _nextionCmd("p0.pic=2");
    }

    // ── Eventos de tag ────────────────────────────────────
    if (xQueueReceive(qTagData, &ev, pdMS_TO_TICKS(500)) == pdTRUE) {
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
      }
    }
  }
}