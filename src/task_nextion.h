#pragma once
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
  _nextionCmd(String("status_cart.") + obj + ".txt=\"" + _sanitizar(txt) + "\"");
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
  _setText("IdCart1",     "---");
  _setText("StatusCart1", "Sem tag");
  _setText("CiclosCart1", "---");
  _setText("VidaCart1",   "---");
  _setText("SerialCart1", "---");
}

static void _nextionMostrar(const TagData &d) {
  _setText("IdCart1",     String(d.id));
  _setText("StatusCart1", _statusTexto(d.status));
  _setText("CiclosCart1", String(d.ciclos));
  _setText("VidaCart1",   String(d.vida));
  _setText("SerialCart1", String(d.serial));
}

// =========================
// TASK NEXTION
// Roda no Core 1, prioridade 2
// Bloqueia na fila — atualiza o display apenas quando há evento novo
// =========================
void taskNextion(void *param) {
  Serial2.begin(9600, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
  vTaskDelay(pdMS_TO_TICKS(500));   // aguarda Nextion inicializar
  _nextionLimpar();
  Serial.println("[Nextion] Pronto.");

  TagEvent ev;

  for (;;) {
    // Bloqueia até chegar evento (máx 1s para manter display responsivo)
    if (xQueueReceive(qTagData, &ev, pdMS_TO_TICKS(1000)) == pdTRUE) {
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
          _setText("StatusCart1", "Erro");
          break;
      }
    }
    // Se timeout (1s sem evento) — pode reenviar estado atual se necessário
    // (útil após reinicialização do Nextion)
  }
}