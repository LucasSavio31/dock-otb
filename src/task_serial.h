#pragma once
#include "shared.h"

// =========================
// HELPERS SERIAL
// =========================
static String _statusTextoSerial(uint8_t s) {
  switch (s) {
    case 0: return "Vazio";
    case 1: return "OK";
    case 2: return "Em uso";
    case 3: return "Bloqueado";
    case 4: return "Erro";
    default: return "Desconhecido";
  }
}

static void _mostrarDados(const TagData &d) {
  Serial.println();
  Serial.println("======= DADOS =======");
  Serial.print("ID:      "); Serial.println(d.id);
  Serial.print("Vida:    "); Serial.println(d.vida);
  Serial.print("Ciclos:  "); Serial.println(d.ciclos);
  Serial.print("Status:  ");
  Serial.print(d.status);
  Serial.print(" ("); Serial.print(_statusTextoSerial(d.status)); Serial.println(")");
  Serial.print("Serial:  "); Serial.println(d.serial);
  Serial.println("=====================");
}

static void _mostrarMenu() {
  Serial.println();
  Serial.println("1 - Ler");
  Serial.println("2 - Gravar");
  Serial.println("3 - Resetar tag");
  Serial.print("> ");
}

// Lê linha bloqueando a task (não o sistema)
static String _lerLinha(uint32_t timeoutMs = 30000) {
  String s;
  uint32_t t0 = millis();
  for (;;) {
    // Verifica se tag ainda está presente
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(5)) == pdTRUE) {
      bool p = gTag.presente;
      xSemaphoreGive(mutexTag);
      if (!p && s.length() == 0) return "";
    }

    if (millis() - t0 > timeoutMs) return "";   // timeout de input

    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        s.trim();
        return s;
      }
      s += c;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static uint16_t _lerNumero(const char *prompt) {
  for (;;) {
    Serial.println(prompt);
    String s = _lerLinha();
    if (s.length() == 0) return 0;
    bool ok = true;
    for (size_t i = 0; i < s.length(); i++) {
      if (!isDigit((unsigned char)s[i])) { ok = false; break; }
    }
    if (ok) return (uint16_t)s.toInt();
    Serial.println("Digite apenas numeros.");
  }
}

static String _lerTexto(const char *prompt, size_t maxLen) {
  for (;;) {
    Serial.println(prompt);
    String s = _lerLinha();
    if (s.length() > 0 && s.length() <= maxLen) return s;
    Serial.print("Invalido. Max "); Serial.print(maxLen); Serial.println(" chars.");
  }
}

// =========================
// AGUARDA resposta da taskNFC via qSerialResp (fila exclusiva)
// qTagData NÃO é usada aqui — taskNextion consome aquela fila
// =========================
static bool _aguardarResposta(TagEvent &ev, uint32_t timeoutMs = 4000) {
  return xQueueReceive(qSerialResp, &ev, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

// =========================
// TASK SERIAL
// Roda no Core 1, prioridade 2
// Implementa menu bloqueante sem interferir no polling NFC
// =========================
void taskSerial(void *param) {
  Serial.println("[Serial] Menu pronto. Aguardando tag...");

  bool menuMostrado = false;

  for (;;) {
    // --- Verifica se há tag presente ---
    bool presente = false;
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(10)) == pdTRUE) {
      presente = gTag.presente;
      xSemaphoreGive(mutexTag);
    }

    if (!presente) {
      menuMostrado = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // --- Mostra menu uma vez por tag ---
    if (!menuMostrado) {
      Serial.println();
      Serial.println("=== TAG DETECTADA ===");

      if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
        Serial.print("UID: ");
        for (uint8_t i = 0; i < gTag.uidLen; i++) {
          if (gTag.uid[i] < 0x10) Serial.print("0");
          Serial.print(gTag.uid[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
        if (gTag.cacheValido) _mostrarDados(gTag.cache);
        xSemaphoreGive(mutexTag);
      }

      Serial.println();
      Serial.println("Legenda: 0=Vazio 1=OK 2=Em uso 3=Bloqueado 4=Erro");
      _mostrarMenu();
      menuMostrado = true;
    }

    // --- Aguarda opção ---
    if (!Serial.available()) {
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    String op = _lerLinha();
    op.trim();

    // Verifica tag ainda presente após leitura
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(10)) == pdTRUE) {
      presente = gTag.presente;
      xSemaphoreGive(mutexTag);
    }
    if (!presente) { menuMostrado = false; continue; }

    if (op == "1") {
      // --- LER ---
      SerialCmd cmd{ SerialCmd::CMD_LER, {} };
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));

      TagEvent ev;
      if (_aguardarResposta(ev)) {
        if (ev.type == TagEvent::TAG_LIDA) {
          Serial.println("Leitura OK:");
          _mostrarDados(ev.data);
        } else {
          Serial.println("Falha ao ler a tag.");
        }
      } else {
        Serial.println("Timeout aguardando leitura.");
      }
      _mostrarMenu();
    }
    else if (op == "2") {
      // --- GRAVAR ---
      SerialCmd cmd{ SerialCmd::CMD_GRAVAR, {} };

      String id     = _lerTexto("ID:", 16);
      if (id.length() == 0) { menuMostrado = false; continue; }

      uint16_t ciclos = _lerNumero("Ciclos:");
      uint16_t vida   = _lerNumero("Vida:");
      String serial   = _lerTexto("Serial:", 16);
      if (serial.length() == 0) { menuMostrado = false; continue; }

      cmd.payload.vida   = vida;
      cmd.payload.ciclos = ciclos;
      cmd.payload.status = 1;
      strncpy(cmd.payload.id,     id.c_str(),     sizeof(cmd.payload.id)     - 1);
      strncpy(cmd.payload.serial, serial.c_str(), sizeof(cmd.payload.serial) - 1);

      Serial.println(); Serial.println("Gravando...");
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));

      TagEvent ev;
      if (_aguardarResposta(ev, 4000)) {
        if (ev.type == TagEvent::TAG_GRAVADA) {
          Serial.println("Gravado com sucesso:");
          _mostrarDados(ev.data);
        } else {
          Serial.println("Falha ao gravar.");
        }
      } else {
        Serial.println("Timeout na gravacao.");
      }
      _mostrarMenu();
    }
    else if (op == "3") {
      // --- RESETAR ---
      Serial.println("Resetando...");
      SerialCmd cmd{ SerialCmd::CMD_RESETAR, {} };
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));

      TagEvent ev;
      if (_aguardarResposta(ev, 4000)) {
        if (ev.type == TagEvent::TAG_RESETADA) {
          Serial.println("Tag resetada:");
          _mostrarDados(ev.data);
        } else {
          Serial.println("Falha ao resetar.");
        }
      } else {
        Serial.println("Timeout no reset.");
      }
      _mostrarMenu();
    }
    else if (op.length() > 0) {
      Serial.println("Opcao invalida.");
      _mostrarMenu();
    }
  }
}