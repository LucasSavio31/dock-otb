#pragma once
// =============================================================
//  task_serial.h — V5
//  Menu NFC original + comandos de controle:
//    b1/b0/b0-100  → bomba PWM
//    v1on/v1off    → válvulas (v1/v2/v3)
//    diag          → diagnóstico geral
// =============================================================
#include "shared.h"

static String _statusTextoSerial(uint8_t s) {
  switch (s) {
    case 0: return "Vazio";   case 1: return "OK";
    case 2: return "Em uso";  case 3: return "Bloqueado";
    case 4: return "Erro";    default: return "Desconhecido";
  }
}

static void _mostrarDados(const TagData &d) {
  Serial.println("\n======= DADOS TAG =======");
  Serial.printf("ID:     %s\n", d.id);
  Serial.printf("Vida:   %d\n", d.vida);
  Serial.printf("Ciclos: %d\n", d.ciclos);
  Serial.printf("Status: %d (%s)\n", d.status, _statusTextoSerial(d.status).c_str());
  Serial.printf("Serial: %s\n", d.serial);
  Serial.println("=========================");
}

static void _mostrarMenu() {
  Serial.println("\n--- TAG ---");
  Serial.println("1       → Ler tag");
  Serial.println("2       → Gravar tag");
  Serial.println("3       → Resetar tag");
  Serial.println("--- CONTROLE ---");
  Serial.println("b1      → Bomba LIGA");
  Serial.println("b0      → Bomba DESLIGA");
  Serial.println("b0-100  → Bomba duty % (ex: b75)");
  Serial.println("v1on/v1off | v2on/v2off | v3on/v3off");
  Serial.println("diag    → Diagnostico");
  Serial.print("> ");
}

static String _lerLinha(uint32_t timeoutMs = 30000) {
  String s;
  uint32_t t0 = millis();
  for (;;) {
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(5)) == pdTRUE) {
      bool p = gTag.presente; xSemaphoreGive(mutexTag);
      if (!p && s.length() == 0) return "";
    }
    if (millis() - t0 > timeoutMs) return "";
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c == '\n') { Serial.println(); s.trim(); return s; }
      if (c == '\b' || c == 127) { // backspace
        if (s.length() > 0) { s.remove(s.length()-1); Serial.print("\b \b"); }
        continue;
      }
      Serial.print(c); // echo
      s += c;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static String _lerLinhaSemTag(uint32_t timeoutMs = 30000) {
  String s;
  uint32_t t0 = millis();
  for (;;) {
    if (millis() - t0 > timeoutMs) return "";
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c == '\n') { Serial.println(); s.trim(); return s; }
      if (c == '\b' || c == 127) { // backspace
        if (s.length() > 0) { s.remove(s.length()-1); Serial.print("\b \b"); }
        continue;
      }
      Serial.print(c); // echo
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
    for (size_t i = 0; i < s.length(); i++) if (!isDigit((unsigned char)s[i])) { ok = false; break; }
    if (ok) return (uint16_t)s.toInt();
    Serial.println("Digite apenas numeros.");
  }
}

static String _lerTexto(const char *prompt, size_t maxLen) {
  for (;;) {
    Serial.println(prompt);
    String s = _lerLinha();
    if (s.length() > 0 && s.length() <= maxLen) return s;
    Serial.printf("Invalido. Max %d chars.\n", (int)maxLen);
  }
}

static bool _aguardarResposta(TagEvent &ev, uint32_t timeoutMs = 4000) {
  return xQueueReceive(qSerialResp, &ev, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void _enviarControle(ControleCmd::Type type, uint8_t payload = 0) {
  ControleCmd cmd{ type, payload };
  xQueueSend(qControleCmd, &cmd, pdMS_TO_TICKS(100));
}

static bool _processarComandoControle(const String &op) {
  // Bomba
  if (op == "b1") { _enviarControle(ControleCmd::BOMBA_ON);  Serial.println(">> Bomba LIGADA");    return true; }
  if (op == "b0") { _enviarControle(ControleCmd::BOMBA_OFF); Serial.println(">> Bomba DESLIGADA"); return true; }
  if (op.startsWith("b") && op.length() > 1) {
    int pct = op.substring(1).toInt();
    if (pct >= 0 && pct <= 100) {
      _enviarControle(ControleCmd::BOMBA_DUTY, (uint8_t)pct);
      Serial.printf(">> Bomba duty=%d%%\n", pct); return true;
    }
  }
  // Válvulas
  if (op == "v1on")  { _enviarControle(ControleCmd::VALVULA_ON,  1); Serial.println(">> Valvula 1 LIGADA");    return true; }
  if (op == "v1off") { _enviarControle(ControleCmd::VALVULA_OFF, 1); Serial.println(">> Valvula 1 DESLIGADA"); return true; }
  if (op == "v2on")  { _enviarControle(ControleCmd::VALVULA_ON,  2); Serial.println(">> Valvula 2 LIGADA");    return true; }
  if (op == "v2off") { _enviarControle(ControleCmd::VALVULA_OFF, 2); Serial.println(">> Valvula 2 DESLIGADA"); return true; }
  if (op == "v3on")  { _enviarControle(ControleCmd::VALVULA_ON,  3); Serial.println(">> Valvula 3 LIGADA");    return true; }
  if (op == "v3off") { _enviarControle(ControleCmd::VALVULA_OFF, 3); Serial.println(">> Valvula 3 DESLIGADA"); return true; }
  // Diagnóstico
  if (op == "diag") {
    Serial.println("\n====== DIAGNOSTICO V5 ======");
    Wire.beginTransmission(0x70);
    Serial.printf("TCA9548A (0x70): %s\n", Wire.endTransmission() == 0 ? "ONLINE" : "OFFLINE");
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
      Serial.printf("Tag presente   : %s\n", gTag.presente ? "SIM" : "NAO");
      if (gTag.presente && gTag.cacheValido) Serial.printf("Tag ID         : %s\n", gTag.cache.id);
      xSemaphoreGive(mutexTag);
    }
    Serial.println("============================\n");
    return true;
  }
  return false;
}

void taskSerial(void *param) {
  Serial.println("[Serial] V5 pronto. Aguardando tag ou comando...");
  bool menuMostrado = false;

  for (;;) {
    bool presente = false;
    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(10)) == pdTRUE) {
      presente = gTag.presente; xSemaphoreGive(mutexTag);
    }

    if (!presente) {
      menuMostrado = false;
      if (Serial.available()) {
        String op = _lerLinhaSemTag(5000);
        op.trim(); op.toLowerCase();
        if (!_processarComandoControle(op) && op.length() > 0)
          Serial.println("(sem cartucho) Comandos: b0/b1/bN | v1on/v1off | diag");
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!menuMostrado) {
      Serial.println("\n=== TAG DETECTADA ===");
      if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(20)) == pdTRUE) {
        Serial.print("UID: ");
        for (uint8_t i = 0; i < gTag.uidLen; i++) {
          if (gTag.uid[i] < 0x10) Serial.print("0");
          Serial.print(gTag.uid[i], HEX); Serial.print(" ");
        }
        Serial.println();
        if (gTag.cacheValido) _mostrarDados(gTag.cache);
        xSemaphoreGive(mutexTag);
      }
      _mostrarMenu();
      menuMostrado = true;
    }

    if (!Serial.available()) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }

    String op = _lerLinha();
    op.trim(); op.toLowerCase();

    if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(10)) == pdTRUE) {
      presente = gTag.presente; xSemaphoreGive(mutexTag);
    }
    if (!presente) { menuMostrado = false; continue; }

    if (_processarComandoControle(op)) { Serial.print("> "); continue; }

    if (op == "1") {
      SerialCmd cmd{ SerialCmd::CMD_LER, {} };
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
      TagEvent ev;
      if (_aguardarResposta(ev)) {
        if (ev.type == TagEvent::TAG_LIDA) _mostrarDados(ev.data);
        else Serial.println("Falha ao ler.");
      } else Serial.println("Timeout.");
    }
    else if (op == "2") {
      SerialCmd cmd{ SerialCmd::CMD_GRAVAR, {} };
      String id = _lerTexto("ID:", 16);
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
      Serial.println("Gravando...");
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
      TagEvent ev;
      if (_aguardarResposta(ev, 4000)) {
        if (ev.type == TagEvent::TAG_GRAVADA) { Serial.println("OK:"); _mostrarDados(ev.data); }
        else Serial.println("Falha ao gravar.");
      } else Serial.println("Timeout.");
    }
    else if (op == "3") {
      SerialCmd cmd{ SerialCmd::CMD_RESETAR, {} };
      xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
      TagEvent ev;
      if (_aguardarResposta(ev, 4000)) {
        if (ev.type == TagEvent::TAG_RESETADA) { Serial.println("Resetada:"); _mostrarDados(ev.data); }
        else Serial.println("Falha ao resetar.");
      } else Serial.println("Timeout.");
    }
    else if (op.length() > 0) Serial.println("Opcao invalida.");

    _mostrarMenu();
  }
}