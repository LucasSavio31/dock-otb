#pragma once
#include "task_erros.h"
// =============================================================
//  task_serial.h — V5
//  Menu rebranding OTB DockStation
// =============================================================
#include "shared.h"
#include "esp_log.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include <Preferences.h>

// ── Helpers de leitura serial ─────────────────────────────────

static String _statusTextoSerial(uint8_t s) {
  switch (s) {
    case 0: return "Vazio";   case 1: return "OK";
    case 2: return "Em uso";  case 3: return "Bloqueado";
    case 4: return "Erro";    case 5: return "Inativo";
    default: return "Desconhecido";
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
      if (c == '\b' || c == 127) {
        if (s.length() > 0) { s.remove(s.length()-1); Serial.print("\b \b"); }
        continue;
      }
      if ((uint8_t)c < 32 || (uint8_t)c > 126) continue;
      Serial.print(c); s += c;
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
      if (c == 0x1B) return "\x1B";  // ESC
      if (c == '\r') continue;
      if (c == '\n') { Serial.println(); s.trim(); return s; }
      if (c == '\b' || c == 127) {
        if (s.length() > 0) { s.remove(s.length()-1); Serial.print("\b \b"); }
        continue;
      }
      if ((uint8_t)c < 32 || (uint8_t)c > 126) continue;
      Serial.print(c); s += c;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static uint16_t _lerNumero(const char *prompt) {
  for (;;) {
    Serial.println(prompt);
    String s = _lerLinhaSemTag();
    if (s == "\x1B" || s.length() == 0) return 0;
    bool ok = true;
    for (size_t i = 0; i < s.length(); i++) if (!isDigit((unsigned char)s[i])) { ok = false; break; }
    if (ok) return (uint16_t)s.toInt();
    Serial.println("Digite apenas numeros.");
  }
}

static String _lerTexto(const char *prompt, size_t maxLen) {
  for (;;) {
    Serial.println(prompt);
    String s = _lerLinhaSemTag();
    if (s == "\x1B") return "";
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

// ── Menu principal ────────────────────────────────────────────

static void _mostrarMenuPrincipal() {
  Serial.println();
  Serial.println("╔══════════════════════════════════╗");
  Serial.println("║   OTB DockStation — Menu         ║");
  Serial.println("╠══════════════════════════════════╣");
  Serial.println("║  pen    → Canetas  (NFC 1-2-3)   ║");
  Serial.println("║  cart   → Cartuchos (NFC 4-5-6)  ║");
  Serial.println("║  valv   → Valvulas               ║");
  Serial.println("║  pump   → Bomba PWM              ║");
  Serial.println("║  diag   → Diagnostico            ║");
  Serial.println("║  dock   → Info do sistema        ║");
  Serial.println("║  sensor  → Leituras FDC1004      ║");
  Serial.println("║  erros   → Codigos de erro       ║");
  Serial.println("║  i2cscan → Scanner I2C           ║");
  Serial.println("║  ota     → Atualizacao firmware  ║");
  Serial.println("╚══════════════════════════════════╝");
  Serial.print("> ");
}

// ── Submenu PEN / CART ────────────────────────────────────────

static void _menuNFC(bool isCaneta) {
  const char* tipo = isCaneta ? "CANETA" : "CARTUCHO";
  const char* nomesCaneta[3]   = {"Caneta 1  (NFC #1)", "Caneta 2  (NFC #2)", "Caneta 3  (NFC #3)"};
  const char* nomesCartucho[3] = {"Cartucho 1 (NFC #4)", "Cartucho 2 (NFC #5)", "Cartucho 3 (NFC #6)"};
  const char** nomes = isCaneta ? nomesCaneta : nomesCartucho;

  Serial.printf("\n=== %s ===\n", tipo);
  Serial.printf("  1 → %s\n", nomes[0]);
  Serial.printf("  2 → %s\n", nomes[1]);
  Serial.printf("  3 → %s\n", nomes[2]);
  Serial.print("> ");

  String escolha = _lerLinhaSemTag(10000);
  escolha.trim();
  if (escolha != "1" && escolha != "2" && escolha != "3") {
    Serial.println("Opcao invalida."); return;
  }
  int idx = escolha.toInt() - 1;

  uint8_t readerIdx = isCaneta ? (uint8_t)idx : (uint8_t)(idx + 3);
  if (!(nfcReaderOkMask & (1 << readerIdx))) {
    Serial.printf("Leitor %s OFFLINE — nao conectado.\n", nomes[idx]);
    return;
  }

  Serial.printf("\n--- %s ---\n", nomes[idx]);
  Serial.println("  i → Identificar (ler UID)");
  Serial.println("  l → Ler dados");
  Serial.println("  g → Gravar dados");
  Serial.println("  r → Resetar");
  if (!isCaneta) {
    uint8_t si = (uint8_t)idx;
    if (gCartBind[si].uidLen > 0) {
      Serial.print("  u → Desvincular UID (atual:");
      for (uint8_t b = 0; b < gCartBind[si].uidLen; b++) Serial.printf(" %02X", gCartBind[si].uid[b]);
      Serial.println(")");
    } else {
      Serial.println("  [sem vinculo — aceita qualquer tag]");
    }
  }
  Serial.print("> ");

  String acao = _lerLinhaSemTag(10000);
  acao.trim(); acao.toLowerCase();

  if (acao == "i") {
    // Identificar — mostra UID
    SerialCmd cmd{ SerialCmd::CMD_IDENTIFICAR, readerIdx, {} };
    xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
    TagEvent ev;
    if (_aguardarResposta(ev)) {
      if (ev.type != TagEvent::TAG_LIDA || ev.uidLen == 0) {
        Serial.printf("Nenhum %s conectado no leitor.\n", isCaneta ? "caneta" : "cartucho");
        return;
      }
      Serial.printf("\n%s — UID: ", nomes[idx]);
      for (uint8_t i = 0; i < ev.uidLen; i++) {
        if (ev.uid[i] < 0x10) Serial.print("0");
        Serial.print(ev.uid[i], HEX); Serial.print(" ");
      }
      Serial.println();
    } else Serial.println("Timeout.");
  }
  else if (acao == "l") {
    SerialCmd cmd{ SerialCmd::CMD_LER, readerIdx, {} };
    xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
    TagEvent ev;
    if (_aguardarResposta(ev)) {
      if (ev.type == TagEvent::TAG_LIDA) _mostrarDados(ev.data);
      else Serial.printf("Nenhum %s conectado no leitor.\n", isCaneta ? "caneta" : "cartucho");
    } else Serial.println("Timeout.");
  }
  else if (acao == "g") {
    SerialCmd cmd{ SerialCmd::CMD_GRAVAR, readerIdx, {} };
    String id = _lerTexto("ID:", 16);
    if (id.length() == 0) return;
    uint16_t ciclos = _lerNumero("Ciclos:");
    uint16_t vida   = _lerNumero("Vida:");
    String serial   = _lerTexto("Serial:", 16);
    if (serial.length() == 0) return;
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
  else if (acao == "r") {
    Serial.println("Resetando...");
    SerialCmd cmd{ SerialCmd::CMD_RESETAR, readerIdx, {} };
    xQueueSend(qSerialCmd, &cmd, pdMS_TO_TICKS(100));
    TagEvent ev;
    if (_aguardarResposta(ev, 4000)) {
      if (ev.type == TagEvent::TAG_RESETADA) { Serial.println("Resetada:"); _mostrarDados(ev.data); }
      else Serial.println("Falha ao resetar.");
    } else Serial.println("Timeout.");
  }
  else if (acao == "u" && !isCaneta) {
    uint8_t si = (uint8_t)(readerIdx - 3);
    gCartBind[si].uidLen = 0;
    Preferences prefs;
    if (xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(500)) == pdTRUE) {
      prefs.begin("cartbind", false);
      char key[8];
      snprintf(key, sizeof(key), "ulen%u", si);
      prefs.putUChar(key, 0);
      prefs.end();
      xSemaphoreGive(mutexNVS);
    }
    Serial.printf("Leitor %u desvinculado — aceita qualquer tag.\n", readerIdx + 1);
  }
  else { Serial.println("Acao invalida."); }
}

// ── Submenu VALV ──────────────────────────────────────────────

static void _menuValv() {
  for (;;) {
    Serial.println("\n=== VALVULAS === (ESC para voltar)");
    Serial.println("  v1on / v1off");
    Serial.println("  v2on / v2off");
    Serial.println("  v3on / v3off");
    Serial.print("> ");
    String cmd = _lerLinhaSemTag(30000);
    cmd.trim(); cmd.toLowerCase();
    if (cmd == "\x1B" || cmd == "") return;
    if      (cmd == "v1on")  { _enviarControle(ControleCmd::VALVULA_ON,  1); Serial.println("V1 LIGADA");    }
    else if (cmd == "v1off") { _enviarControle(ControleCmd::VALVULA_OFF, 1); Serial.println("V1 DESLIGADA"); }
    else if (cmd == "v2on")  { _enviarControle(ControleCmd::VALVULA_ON,  2); Serial.println("V2 LIGADA");    }
    else if (cmd == "v2off") { _enviarControle(ControleCmd::VALVULA_OFF, 2); Serial.println("V2 DESLIGADA"); }
    else if (cmd == "v3on")  { _enviarControle(ControleCmd::VALVULA_ON,  3); Serial.println("V3 LIGADA");    }
    else if (cmd == "v3off") { _enviarControle(ControleCmd::VALVULA_OFF, 3); Serial.println("V3 DESLIGADA"); }
    else Serial.println("Comando invalido.");
  }
}

// ── Submenu PUMP ──────────────────────────────────────────────

static void _menuPump() {
  for (;;) {
    Serial.println("\n=== BOMBA === (ESC para voltar)");
    Serial.println("  b1       → Liga (duty atual)");
    Serial.println("  b0       → Desliga");
    Serial.println("  b0-100   → Duty % (ex: b75)");
    Serial.print("> ");
    String cmd = _lerLinhaSemTag(30000);
    cmd.trim(); cmd.toLowerCase();
    if (cmd == "\x1B" || cmd == "") return;
    if (cmd == "b1") { _enviarControle(ControleCmd::BOMBA_ON);  Serial.println("Bomba LIGADA");    }
    else if (cmd == "b0") { _enviarControle(ControleCmd::BOMBA_OFF); Serial.println("Bomba DESLIGADA"); }
    else if (cmd.startsWith("b")) {
      int pct = cmd.substring(1).toInt();
      if (pct >= 0 && pct <= 100) {
        _enviarControle(ControleCmd::BOMBA_DUTY, (uint8_t)pct);
        Serial.printf("Bomba duty=%d%%\n", pct);
      } else Serial.println("Valor invalido (0-100).");
    } else Serial.println("Comando invalido.");
  }
}

// ── Submenu DIAG ──────────────────────────────────────────────

static void _menuDiag() {
  Serial.println("\n====== DIAGNOSTICO ======");

  // ── TCA9548A e FDC1004 (I2C) ─────────────────────────────
  bool tcaOk = false;
  if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(200)) == pdTRUE) {
    Wire.beginTransmission(TCA_ADDR);
    tcaOk = (Wire.endTransmission() == 0);

    const char* fdcNomes[3] = {
      "FDC1004 CH0 (Caneta 1)",
      "FDC1004 CH1 (Caneta 2)",
      "FDC1004 CH2 (Caneta 3)"
    };
    for (uint8_t ch = 0; ch < 3; ch++) {
      if (tcaOk) {
        Wire.beginTransmission(TCA_ADDR);
        Wire.write(1 << ch);
        Wire.endTransmission();
        
        Wire.beginTransmission(0x50);
        bool isFDC = (Wire.endTransmission() == 0);
        Wire.beginTransmission(0x48);
        bool isAD = (Wire.endTransmission() == 0);

        if (isFDC)      Serial.printf("FDC1004 CH%d (Caneta %d) : ONLINE\n", ch, ch+1);
        else if (isAD)  Serial.printf("AD7747  CH%d (Caneta %d) : ONLINE\n", ch, ch+1);
        else            Serial.printf("Sensor  CH%d (Caneta %d) : OFFLINE\n", ch, ch+1);
      } else {
        Serial.printf("Sensor  CH%d (Caneta %d) : OFFLINE (TCA ausente)\n", ch, ch+1);
      }
    }
    // Reset seletor TCA
    Wire.beginTransmission(TCA_ADDR); Wire.write(0x00); Wire.endTransmission();
    xSemaphoreGive(mutexI2C);
  } else {
    Serial.println("I2C ocupado — diagnostico I2C ignorado.");
    tcaOk = false;
  }
  Serial.printf("TCA9548A (0x70)  : %s\n", tcaOk ? "ONLINE" : "OFFLINE");

  // ── Nextion (Serial2) ─────────────────────────────────────
  while (Serial2.available()) Serial2.read();
  Serial2.print("sendme");
  Serial2.write(0xFF); Serial2.write(0xFF); Serial2.write(0xFF);
  vTaskDelay(pdMS_TO_TICKS(150));
  bool nextionOk = false;
  uint8_t nbuf[8] = {0}; uint8_t nidx = 0;
  while (Serial2.available() && nidx < 8) nbuf[nidx++] = Serial2.read();
  for (uint8_t i = 0; i < nidx; i++) if (nbuf[i] == 0x66) { nextionOk = true; break; }
  Serial.printf("Nextion Serial2  : %s\n", nextionOk ? "ONLINE" : "OFFLINE");

  // ── NFC estado atual ──────────────────────────────────────
  if (xSemaphoreTake(mutexTag, pdMS_TO_TICKS(50)) == pdTRUE) {
    bool presente = gTag.presente;
    xSemaphoreGive(mutexTag);
    Serial.printf("Tag presente     : %s\n", presente ? "SIM" : "NAO");
  }

  // ── PN532 SPI — toma mutex para nao conflitar com taskNFC ─
  Serial.println("--- NFC SPI ---");
  const uint8_t nfcCS[]    = {13, 14, 4, 5, 15, 32};
  const char*   nfcNomes[] = {"D13","D14","D4 ","D5 ","D15","D32"};

  if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(3000)) == pdTRUE) {
    for (uint8_t i = 0; i < 6; i++) {
      if (nfcCS[i] == LED2) continue;  // D2 e o LED — nao toca
      pinMode(nfcCS[i], OUTPUT); digitalWrite(nfcCS[i], HIGH);
    }
    for (uint8_t i = 0; i < 6; i++) {
      if (nfcCS[i] == LED2) {
        Serial.printf("PN532 #%d CS=%s : OFFLINE (pino LED)\n", i+1, nfcNomes[i]);
        continue;
      }
      Adafruit_PN532 nfcTmp(nfcCS[i], &SPI);
      nfcTmp.begin();
      uint32_t fw = nfcTmp.getFirmwareVersion();
      if (fw) Serial.printf("PN532 #%d CS=%s : ONLINE  FW=%d.%d\n", i+1, nfcNomes[i], (fw>>16)&0xFF, (fw>>8)&0xFF);
      else    Serial.printf("PN532 #%d CS=%s : OFFLINE\n", i+1, nfcNomes[i]);
      vTaskDelay(pdMS_TO_TICKS(30));
    }
    xSemaphoreGive(mutexSPI);
    // Chips foram reiniciados pelo begin() — sinaliza taskNFC para reconfigurar
    nfcReinitPending = true;
  } else {
    Serial.println("SPI ocupado — diagnostico NFC ignorado.");
  }

  Serial.println("=========================\n");
}

// ── Submenu DOCK ──────────────────────────────────────────────

static void _menuDock() {
  Serial.println("\n====== SISTEMA DOCKSTATION ======");

  // CPU
  Serial.printf("CPU Freq         : %d MHz\n", getCpuFrequencyMhz());

  // Memória
  Serial.printf("Heap livre       : %d bytes\n",   esp_get_free_heap_size());
  Serial.printf("Heap minima      : %d bytes\n",   esp_get_minimum_free_heap_size());
  Serial.printf("Heap total       : %d bytes\n",   ESP.getHeapSize());
  Serial.printf("PSRAM livre      : %d bytes\n",   ESP.getFreePsram());

  // Flash
  Serial.printf("Flash tamanho    : %d bytes\n",   ESP.getFlashChipSize());
  Serial.printf("Sketch tamanho   : %d bytes\n",   ESP.getSketchSize());
  Serial.printf("Sketch livre     : %d bytes\n",   ESP.getFreeSketchSpace());

  // Chip
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  Serial.printf("Chip modelo      : Dockstation rev %d\n", chip.revision);
  Serial.printf("Nucleos          : %d\n",            chip.cores);
  Serial.printf("WiFi + BT        : %s\n",
                (chip.features & CHIP_FEATURE_BT) ? "SIM" : "NAO");

  // Uptime
  uint32_t s = millis() / 1000;
  Serial.printf("Uptime           : %02d:%02d:%02d\n", s/3600, (s%3600)/60, s%60);
  Serial.printf("Serial Dock      : %s\n", gSerialDock);
  Serial.printf("Firmware         : %s\n", FIRMWARE_VERSION);

  // Temperatura interna (via hall sensor como proxy)
  Serial.printf("Hall sensor      : %d\n", hallRead());

  // Tasks FreeRTOS
  Serial.printf("Tasks ativas     : %d\n", uxTaskGetNumberOfTasks());
  Serial.println("===========================\n");
}

// ── Submenu SENSOR ────────────────────────────────────────────

static void _menuSensor() {
  Serial.println("\n=== SENSOR DE NIVEL ===");
  Serial.println("Qual canal deseja ler?");
  Serial.println("  1 → Canal 1 (CH0)");
  Serial.println("  2 → Canal 2 (CH1)");
  Serial.println("  3 → Canal 3 (CH2)");
  Serial.println("  0 → Todos os 3");
  Serial.print("> ");

  String escolha = _lerLinhaSemTag(10000);
  escolha.trim();
  int8_t chEscolhido = -1;
  if      (escolha == "1") chEscolhido = 0;
  else if (escolha == "2") chEscolhido = 1;
  else if (escolha == "3") chEscolhido = 2;
  else if (escolha == "0") chEscolhido = -1;
  else { Serial.println("Opcao invalida."); return; }

  Serial.println("\nLeitura — pressione ENTER para sair");
  Serial.println("CH    RAW           pF              Nivel  STATUS");
  Serial.println("---   -----------   ------------  -------  ------");

  while (true) {
    if (Serial.available()) { char c = Serial.read(); if (c == '\n' || c == '\r') break; }

    uint8_t chInicio = (chEscolhido >= 0) ? (uint8_t)chEscolhido : 0;
    uint8_t chFim    = (chEscolhido >= 0) ? (uint8_t)chEscolhido : 2;

    for (uint8_t ch = chInicio; ch <= chFim; ch++) {
      NivelState n = {};
      if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
        n = gNivel[ch];
        xSemaphoreGive(mutexNivel);
      }
      if (n.leituraOk)
        Serial.printf("CH%d   %-11ld   %+.6f pF   %5.1f%%   OK\n",  ch, n.rawAtual, n.pFAtual, n.nivelPct);
      else
        Serial.printf("CH%d   --            --              --      ERRO\n", ch);
    }
    Serial.println("---");
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  Serial.println("=== FIM LEITURA ===\n");
}

// ── Submenu I2CSCAN ───────────────────────────────────────────

static void _menuI2CScan() {
  if (!hTaskI2CScan) { Serial.println("Task I2CScan nao disponivel."); return; }
  Serial.println("Scanner I2C continuo — pressione ENTER ou ESC para sair\n");

  for (;;) {
    xTaskNotifyGive(hTaskI2CScan);

    // Aguarda conclusão, verificando ESC a cada 100 ms
    bool sair = false;
    for (uint16_t i = 0; i < 250; i++) {
      if (xSemaphoreTake(semI2CScanDone, pdMS_TO_TICKS(100)) == pdTRUE) break;
      while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r' || c == 0x1B) { sair = true; break; }
      }
      if (sair) break;
    }
    if (sair) break;

    // Checa ESC entre scans antes de aguardar o próximo ciclo
    vTaskDelay(pdMS_TO_TICKS(500));
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r' || c == 0x1B) { sair = true; break; }
    }
    if (sair) break;
  }

  Serial.println("Scanner encerrado.\n");
}

// ── Submenu NVS ───────────────────────────────────────────────
// args: string ORIGINAL (case preservado) após "nvs "
// Comandos: get activation / set activation <json>
//           clear activation / get usage / set usage <ms> / clear usage
//           get recharge_count / set recharge_count <n> / clear recharge_count

static void _menuNvs(const String& args) {
  if (!mutexNVS || xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) != pdTRUE) {
    Serial.println("NVS_ERR:busy");
    return;
  }
  Preferences prefs;
  if (!prefs.begin("otb-dock", false)) {
    xSemaphoreGive(mutexNVS);
    Serial.println("NVS_ERR:open_failed");
    return;
  }

  if (args.startsWith("get activation")) {
    String val = prefs.getString("activ", "null");
    Serial.print("NVS_ACTIVATION:");
    Serial.println(val);

  } else if (args.startsWith("set activation ")) {
    String json = args.substring(15);
    json.trim();
    if (json.length() > 0 && json.length() < 4000) {
      prefs.putString("activ", json);
      Serial.println("NVS_OK:activation");
    } else {
      Serial.println("NVS_ERR:json_too_large");
    }

  } else if (args.startsWith("clear activation")) {
    prefs.remove("activ");
    Serial.println("NVS_OK:activation_cleared");

  } else if (args.startsWith("get usage")) {
    uint64_t ms = prefs.getULong64("usage_ms", 0);
    Serial.printf("NVS_USAGE:%llu\n", ms);

  } else if (args.startsWith("set usage ")) {
    String msStr = args.substring(10);
    msStr.trim();
    uint64_t ms = (uint64_t)strtoull(msStr.c_str(), nullptr, 10);
    prefs.putULong64("usage_ms", ms);
    Serial.println("NVS_OK:usage");

  } else if (args.startsWith("clear usage")) {
    prefs.remove("usage_ms");
    Serial.println("NVS_OK:usage_cleared");

  } else if (args.startsWith("get recharge_count")) {
    uint32_t count = prefs.getUInt("rechg_cnt", 0);
    Serial.printf("NVS_RECHARGE_COUNT:%u\n", count);

  } else if (args.startsWith("set recharge_count ")) {
    String countStr = args.substring(19);
    countStr.trim();
    uint32_t count = (uint32_t)strtoul(countStr.c_str(), nullptr, 10);
    prefs.putUInt("rechg_cnt", count);
    Serial.println("NVS_OK:recharge_count");

  } else if (args.startsWith("clear recharge_count")) {
    prefs.remove("rechg_cnt");
    Serial.println("NVS_OK:recharge_count_cleared");

  } else {
    Serial.println("NVS_ERR:unknown");
  }

  prefs.end();
  xSemaphoreGive(mutexNVS);
}

// =========================
// TASK SERIAL — V5
// =========================
static void _menuErros() {
  uint16_t    codigos[ERR_COUNT] = {0};
  const char* descs[ERR_COUNT]   = {nullptr};
  uint8_t count = erroGetAtivos(codigos, descs, ERR_COUNT - 1);

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  CODIGOS DE ERRO — OTB DockStation   ║");
  Serial.println("╠══════════════════════════════════════╣");

  if (count == 0) {
    Serial.println("║  Nenhum erro ativo                   ║");
  } else {
    for (uint8_t i = 0; i < count; i++) {
      char linha[64];
      snprintf(linha, sizeof(linha), "  E%03d  %s", codigos[i], descs[i]);
      Serial.printf("║  %-36s  ║\n", linha);
    }
  }
  Serial.println("╚══════════════════════════════════════╝");
  Serial.printf("  Total: %d erro(s) ativo(s)\n\n", count);
}

// ── Submenu CALIB ─────────────────────────────────────────────
// Comandos: calib read 1|2|3
//           calib get  1|2|3
//           calib set  1 <vazio_pf> <cheio_pf> <step>
//           calib reset 1|2|3
static void _menuCalib(const String& args) {

  if (args.startsWith("read ")) {
    int ch = args.substring(5).toInt() - 1;
    if (ch < 0 || ch > 2) { Serial.println("CALIB_ERR:invalid_channel"); return; }

    NivelState n = {};
    if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(30)) == pdTRUE) {
      n = gNivel[ch]; xSemaphoreGive(mutexNivel);
    }

    uint8_t ct = gChipCanalTipo[ch];
    const char* chipNome = (ct == 1) ? "FDC1004" : (ct == 2) ? "AD7747" : "NONE";

    // FDC1004: CAPDAC hardware subtrai — pFAtual ja compensado
    // AD7747:  CAPDAC hardware adiciona (single-ended); subtrair aqui para exibir valor real
    float pfDisp = n.pFAtual;
    if (ct == 2 && xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (gCalib[ch].capdacEn) pfDisp -= gCalib[ch].capdacPf;
      xSemaphoreGive(mutexCalib);
    }

    Serial.printf("CALIB_READ:%d,%s,%ld,%.4f,%.1f,%d\n",
      ch, chipNome, (long)n.rawAtual, pfDisp, n.nivelPct, n.leituraOk ? 1 : 0);
  }

  else if (args.startsWith("get ")) {
    int ch = args.substring(4).toInt() - 1;
    if (ch < 0 || ch > 2) { Serial.println("CALIB_ERR:invalid_channel"); return; }

    bool    valid = false;
    float   vazio = 0.0f, cheio = 1.0f;
    uint8_t step  = 10;

    if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(100)) == pdTRUE) {
      valid = gCalib[ch].valid;
      if (valid) {
        vazio = gCalib[ch].vazioPf;
        cheio = gCalib[ch].cheioPf;
        step  = gCalib[ch].step;
      }
      xSemaphoreGive(mutexCalib);
    }

    if (!valid) {
      uint8_t ct = gChipCanalTipo[ch];
      if      (ct == 1) { vazio = NIVEL_PF_VAZIO_FDC; cheio = NIVEL_PF_CHEIO_FDC; }
      else if (ct == 2) { vazio = NIVEL_PF_VAZIO_AD;  cheio = NIVEL_PF_CHEIO_AD;  }
    }

    Serial.printf("CALIB_CONFIG:%d,%.4f,%.4f,%d,%d\n",
      ch, vazio, cheio, (int)step, valid ? 1 : 0);
  }

  else if (args.startsWith("set ")) {
    // formato: set <ch_1-3> <vazio_pf> <cheio_pf> <step>
    String rest = args.substring(4); rest.trim();

    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) { Serial.println("CALIB_SAVE_ERR:0,invalid_format"); return; }
    int ch = rest.substring(0, sp1).toInt() - 1;
    if (ch < 0 || ch > 2) { Serial.println("CALIB_SAVE_ERR:0,invalid_channel"); return; }

    rest = rest.substring(sp1 + 1); rest.trim();
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) { Serial.printf("CALIB_SAVE_ERR:%d,missing_cheio\n", ch); return; }
    float vazio = rest.substring(0, sp2).toFloat();

    rest = rest.substring(sp2 + 1); rest.trim();
    int sp3 = rest.indexOf(' ');
    float   cheio;
    uint8_t step;
    if (sp3 < 0) {
      cheio = rest.toFloat(); step = 10;
    } else {
      cheio = rest.substring(0, sp3).toFloat();
      int sv = rest.substring(sp3 + 1).toInt();
      if (sv < 1 || sv > 100)   { Serial.printf("CALIB_SAVE_ERR:%d,invalid_step\n",      ch); return; }
      if (100 % sv != 0)         { Serial.printf("CALIB_SAVE_ERR:%d,step_not_divisor\n",  ch); return; }
      step = (uint8_t)sv;
    }

    if (vazio < -50.0f || vazio > 50.0f || cheio < -50.0f || cheio > 50.0f) {
      Serial.printf("CALIB_SAVE_ERR:%d,pf_out_of_range\n",  ch); return; }
    if (cheio <= vazio) {
      Serial.printf("CALIB_SAVE_ERR:%d,cheio_lte_vazio\n",  ch); return; }
    if ((cheio - vazio) < 0.01f) {
      Serial.printf("CALIB_SAVE_ERR:%d,span_too_small\n",   ch); return; }

    if (!mutexNVS || xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) != pdTRUE) {
      Serial.printf("CALIB_SAVE_ERR:%d,nvs_busy\n", ch); return; }
    Preferences prefs;
    if (!prefs.begin("otb-dock", false)) {
      xSemaphoreGive(mutexNVS);
      Serial.printf("CALIB_SAVE_ERR:%d,nvs_open_failed\n", ch); return; }
    char kv[8], kc[8], ks[8], kok[8];
    snprintf(kv,  sizeof(kv),  "cal_v%d",  ch);
    snprintf(kc,  sizeof(kc),  "cal_c%d",  ch);
    snprintf(ks,  sizeof(ks),  "cal_s%d",  ch);
    snprintf(kok, sizeof(kok), "cal_ok%d", ch);
    size_t r1 = prefs.putFloat(kv,  vazio);
    size_t r2 = prefs.putFloat(kc,  cheio);
    size_t r3 = prefs.putUInt(ks,   (uint32_t)step);
    size_t r4 = prefs.putUChar(kok, 1);
    prefs.end();
    xSemaphoreGive(mutexNVS);

    if (!r1 || !r2 || !r3 || !r4) {
      Serial.printf("CALIB_SAVE_ERR:%d,nvs_write_failed\n", ch); return; }

    if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(200)) == pdTRUE) {
      gCalib[ch].vazioPf = vazio;
      gCalib[ch].cheioPf = cheio;
      gCalib[ch].step    = step;
      gCalib[ch].valid   = true;
      xSemaphoreGive(mutexCalib);
    }

    Serial.printf("CALIB_SAVE_OK:%d\n", ch);
    logdbPublishf("Serial", "Calibracao", LOG_SUCCESS, "Calibracao salva na caneta %u.", (unsigned)(ch + 1));
  }

  else if (args.startsWith("reset ")) {
    int ch = args.substring(6).toInt() - 1;
    if (ch < 0 || ch > 2) { Serial.println("CALIB_RESET_ERR:0,invalid_channel"); return; }

    if (!mutexNVS || xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) != pdTRUE) {
      Serial.printf("CALIB_RESET_ERR:%d,nvs_busy\n", ch); return; }
    Preferences prefs;
    if (!prefs.begin("otb-dock", false)) {
      xSemaphoreGive(mutexNVS);
      Serial.printf("CALIB_RESET_ERR:%d,nvs_open_failed\n", ch); return; }
    char kv[8], kc[8], ks[8], kok[8], kde[9], kdp[9];
    snprintf(kv,  sizeof(kv),  "cal_v%d",   ch);
    snprintf(kc,  sizeof(kc),  "cal_c%d",   ch);
    snprintf(ks,  sizeof(ks),  "cal_s%d",   ch);
    snprintf(kok, sizeof(kok), "cal_ok%d",  ch);
    snprintf(kde, sizeof(kde), "cdac_e%d",  ch);
    snprintf(kdp, sizeof(kdp), "cdac_p%d",  ch);
    prefs.remove(kv);
    prefs.remove(kc);
    prefs.remove(ks);
    prefs.remove(kok);
    prefs.remove(kde);
    prefs.remove(kdp);
    prefs.end();
    xSemaphoreGive(mutexNVS);

    if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(200)) == pdTRUE) {
      gCalib[ch].vazioPf  = 0.0f;
      gCalib[ch].cheioPf  = 0.0f;
      gCalib[ch].step     = 10;
      gCalib[ch].valid    = false;
      gCalib[ch].capdacEn = false;
      gCalib[ch].capdacPf = 0.0f;
      xSemaphoreGive(mutexCalib);
    }

    Serial.printf("CALIB_RESET_OK:%d\n", ch);
    logdbPublishf("Serial", "Calibracao", LOG_WARN, "Calibracao resetada na caneta %u.", (unsigned)(ch + 1));
  }

  else if (args.startsWith("capdac ")) {
    String sub = args.substring(7); sub.trim();

    if (sub.startsWith("get ")) {
      int ch = sub.substring(4).toInt() - 1;
      if (ch < 0 || ch > 2) { Serial.println("CALIB_CAPDAC_ERR:0,invalid_channel"); return; }
      bool  en  = false;
      float pfv = 0.0f;
      if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(100)) == pdTRUE) {
        en  = gCalib[ch].capdacEn;
        pfv = gCalib[ch].capdacPf;
        xSemaphoreGive(mutexCalib);
      }
      Serial.printf("CALIB_CAPDAC:%d,%d,%.4f\n", ch, en ? 1 : 0, pfv);

    } else if (sub.startsWith("set ")) {
      // formato: set <ch_1-3> <0|1> <pf>
      String rest = sub.substring(4); rest.trim();
      int sp1 = rest.indexOf(' ');
      if (sp1 < 0) { Serial.println("CALIB_CAPDAC_ERR:0,invalid_format"); return; }
      int ch = rest.substring(0, sp1).toInt() - 1;
      if (ch < 0 || ch > 2) { Serial.println("CALIB_CAPDAC_ERR:0,invalid_channel"); return; }
      rest = rest.substring(sp1 + 1); rest.trim();
      int sp2 = rest.indexOf(' ');
      if (sp2 < 0) { Serial.printf("CALIB_CAPDAC_ERR:%d,missing_pf\n", ch); return; }
      bool  en  = rest.substring(0, sp2).toInt() != 0;
      float pfv = rest.substring(sp2 + 1).toFloat();
      if (pfv < 0.0f || pfv > 200.0f) {
        Serial.printf("CALIB_CAPDAC_ERR:%d,pf_out_of_range\n", ch); return;
      }

      if (!mutexNVS || xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) != pdTRUE) {
        Serial.printf("CALIB_CAPDAC_ERR:%d,nvs_busy\n", ch); return; }
      Preferences prefs;
      if (!prefs.begin("otb-dock", false)) {
        xSemaphoreGive(mutexNVS);
        Serial.printf("CALIB_CAPDAC_ERR:%d,nvs_open_failed\n", ch); return; }
      char kde[9], kdp[9];
      snprintf(kde, sizeof(kde), "cdac_e%d", ch);
      snprintf(kdp, sizeof(kdp), "cdac_p%d", ch);
      size_t r1 = prefs.putUChar(kde, en ? 1 : 0);
      size_t r2 = prefs.putFloat(kdp, pfv);
      prefs.end();
      xSemaphoreGive(mutexNVS);
      if (!r1 || !r2) {
        Serial.printf("CALIB_CAPDAC_ERR:%d,nvs_write_failed\n", ch); return;
      }

      if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(200)) == pdTRUE) {
        gCalib[ch].capdacEn = en;
        gCalib[ch].capdacPf = pfv;
        xSemaphoreGive(mutexCalib);
      }
      // Sinaliza taskSensor para re-inicializar o chip com novo CAPDAC de hardware
      gCalibDirty[ch] = true;
      Serial.printf("CALIB_CAPDAC_OK:%d\n", ch);
      logdbPublishf("Serial", "CAPDAC", LOG_SUCCESS, "CAPDAC atualizado na caneta %u.", (unsigned)(ch + 1));
    } else {
      Serial.println("CALIB_CAPDAC_ERR:0,unknown_subcommand");
    }
  }

  else {
    Serial.println("CALIB_ERR:unknown_subcommand");
  }
}

static void _menuLogDb(const String& args) {
  String cmd = args;
  cmd.trim();

  if (cmd == "summary") {
    if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(100)) == pdTRUE) {
      Serial.printf("LOGDB_SUMMARY:%u,%u,%lu\n",
                    (unsigned)gLogCount, (unsigned)gLogBootId, (unsigned long)(gLogNextSeq ? gLogNextSeq - 1 : 0));
      xSemaphoreGive(mutexLogDb);
    } else {
      Serial.println("LOGDB_ERR:busy");
    }
    return;
  }

  if (cmd.startsWith("time ")) {
    String epochStr = cmd.substring(5);
    epochStr.trim();
    uint32_t epoch = (uint32_t)strtoul(epochStr.c_str(), nullptr, 10);
    logdbSetEpoch(epoch);
    Serial.println("LOGDB_OK:time");
    return;
  }

  if (cmd == "clear") {
    logdbClear();
    Serial.println("LOGDB_OK:clear");
    return;
  }

  if (cmd == "flush") {
    logdbForceFlush();
    Serial.println("LOGDB_OK:flush");
    return;
  }

  if (cmd.startsWith("list")) {
    int offset = 0;
    int limit  = LOGDB_CAPACITY;
    int sp1 = cmd.indexOf(' ');
    if (sp1 >= 0) {
      String rest = cmd.substring(sp1 + 1);
      rest.trim();
      int sp2 = rest.indexOf(' ');
      if (sp2 < 0) offset = rest.toInt();
      else {
        offset = rest.substring(0, sp2).toInt();
        limit  = rest.substring(sp2 + 1).toInt();
      }
    }
    if (offset < 0) offset = 0;
    if (limit <= 0 || limit > LOGDB_CAPACITY) limit = LOGDB_CAPACITY;

    if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(150)) != pdTRUE) {
      Serial.println("LOGDB_ERR:busy");
      return;
    }

    const int available = (int)gLogCount;
    const int startIdx  = offset;
    const int endIdx    = min(available, startIdx + limit);
    for (int i = startIdx; i < endIdx; i++) {
      const uint16_t idx = (uint16_t)((gLogHead + i) % LOGDB_CAPACITY);
      if (!gLogEntries) break;
      const StoredLogEntry& e = gLogEntries[idx];
      Serial.printf("LOGDB_ENTRY:%lu|%lu|%lu|%lu|%u|%s|%s|%s\n",
                    (unsigned long)e.seq,
                    (unsigned long)e.bootId,
                    (unsigned long)e.uptimeSec,
                    (unsigned long)e.epochSec,
                    (unsigned)e.severity,
                    e.origin,
                    e.type,
                    e.description);
    }
    Serial.printf("LOGDB_END:%d\n", endIdx - startIdx);
    xSemaphoreGive(mutexLogDb);
    return;
  }

  Serial.println("LOGDB_ERR:unknown");
}

static bool _opPrincipalConhecida(const String &op) {
  return op == "pen"     || op == "cart"   || op == "valv"  ||
         op == "pump"    || op == "diag"   || op == "dock"  ||
         op == "sensor"  || op == "erros"  || op == "i2cscan" ||
         op.startsWith("nvs ") || op.startsWith("recharge ") ||
         op.startsWith("calib ") || op.startsWith("logdb ") ||
         op.startsWith("unlock ") || op == "lock";
}

static bool _linhaEcoOuLixo(const String &raw, const String &op) {
  if (op.length() == 0 || op == ">") return true;
  if (_opPrincipalConhecida(op)) return false;
  if (op.indexOf("otb dockstation") >= 0) return true;

  for (size_t i = 0; i < raw.length(); i++) {
    uint8_t c = (uint8_t)raw[i];
    if (c < 32 || c > 126) return true;
  }
  return false;
}

// ── Menu OTA ──────────────────────────────────────────────────

static void _menuOta(const String& args) {
  // 'args' tem case original (raw) — usar cópia lowercase só para comparar subcomandos
  String a  = args; a.trim();
  String al = a;   al.toLowerCase();

  if (al.length() == 0 || al == "status") {
    OtaCmd cmd{}; cmd.type = OTA_CMD_STATUS;
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }
  if (al == "check") {
    Serial.println("OTA_INFO:Verificando releases no GitHub...");
    OtaCmd cmd{}; cmd.type = OTA_CMD_CHECK;
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }
  if (al == "update") {
    Serial.println("OTA_INFO:Iniciando download e gravacao...");
    OtaCmd cmd{}; cmd.type = OTA_CMD_UPDATE;
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }
  if (al == "rollback") {
    Serial.println("OTA_INFO:Solicitando rollback...");
    OtaCmd cmd{}; cmd.type = OTA_CMD_ROLLBACK;
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }
  if (al == "scan") {
    Serial.println("OTA_INFO:Escaneando redes WiFi...");
    OtaCmd cmd{}; cmd.type = OTA_CMD_WIFI_SCAN;
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }
  // ota wifi <ssid> <senha>  — preserva case original do SSID e senha
  if (al.startsWith("wifi ")) {
    String rest = a.substring(5); rest.trim();
    int sp = rest.indexOf(' ');
    if (sp < 1) {
      // Senha vazia (rede aberta): sp==-1
      if (sp < 0) {
        // sem senha — rede aberta
        OtaCmd cmd{};
        cmd.type = OTA_CMD_WIFI_SET;
        rest.toCharArray(cmd.ssid, sizeof(cmd.ssid));
        cmd.pass[0] = '\0';
        xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
        return;
      }
      Serial.println("OTA_ERR:Uso: ota wifi <ssid> [senha]");
      return;
    }
    OtaCmd cmd{};
    cmd.type = OTA_CMD_WIFI_SET;
    rest.substring(0, sp).toCharArray(cmd.ssid, sizeof(cmd.ssid));
    rest.substring(sp + 1).toCharArray(cmd.pass, sizeof(cmd.pass));
    xQueueSend(qOtaCmd, &cmd, pdMS_TO_TICKS(200));
    return;
  }

  Serial.println("Uso: ota [status|check|update|rollback|scan|wifi <ssid> [senha]]");
}

static void _menuOpMode(const String& arg) {
  if (arg.length() == 0) {
    Serial.printf("OP_MODE:%s\n", gOpMode == OP_MANUAL ? "manual" : "standalone");
    return;
  }
  OpMode newMode;
  if (arg == "standalone")  newMode = OP_STANDALONE;
  else if (arg == "manual") newMode = OP_MANUAL;
  else { Serial.println("Uso: opmode [standalone|manual]"); return; }
  if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
    Preferences prefs;
    if (prefs.begin("otb-dock", false)) {
      prefs.putUChar("op_mode", (uint8_t)newMode);
      prefs.end();
    }
    xSemaphoreGive(mutexNVS);
  }
  gOpMode = newMode;
  Serial.printf("OP_MODE:%s\n", gOpMode == OP_MANUAL ? "manual" : "standalone");
}

void taskSerial(void *param) {
  vTaskDelay(pdMS_TO_TICKS(3500)); // aguarda todas as tasks iniciarem

  // Carrega modo de operação da NVS
  if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
    Preferences prefs;
    if (prefs.begin("otb-dock", true)) {
      gOpMode = (OpMode)prefs.getUChar("op_mode", (uint8_t)OP_STANDALONE);
      prefs.end();
    }
    xSemaphoreGive(mutexNVS);
  }
  Serial.printf("OP_MODE:%s\n", gOpMode == OP_MANUAL ? "manual" : "standalone");

  Serial.println("[Serial] OTB DockStation V5 — pronto.");
  _mostrarMenuPrincipal();

  for (;;) {
    if (!Serial.available()) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }

    String raw = _lerLinhaSemTag(60000);
    raw.trim();
    String op = raw; op.toLowerCase();  // op para comparações; raw preserva case (JSON)

    if (op.length() == 0) { _mostrarMenuPrincipal(); continue; }

    if (_linhaEcoOuLixo(raw, op)) {
      continue;
    }

    if      (op == "pen")    _menuNFC(true);
    else if (op == "cart")   _menuNFC(false);
    else if (op == "valv")   _menuValv();
    else if (op == "pump")   _menuPump();
    else if (op == "diag")   _menuDiag();
    else if (op == "dock")   _menuDock();
    else if (op == "sensor") _menuSensor();
    else if (op == "erros")   _menuErros();
    else if (op == "i2cscan") _menuI2CScan();
    else if (op.startsWith("nvs "))      _menuNvs(raw.substring(4));
    else if (op.startsWith("calib "))    _menuCalib(op.substring(6));
    else if (op.startsWith("logdb "))    _menuLogDb(raw.substring(6));
    else if (op == "ota" || op.startsWith("ota "))  _menuOta(raw.length() > 3 ? raw.substring(4) : "");
    else if (op.startsWith("recharge ")) {
      String arg = op.substring(9); arg.trim();
      if (arg == "stop") {
        RechargeCmd cmd{ RechargeCmd::STOP, 0 };
        xQueueSend(qRechargeCmd, &cmd, pdMS_TO_TICKS(100));
        Serial.println("Recarga encerrada.");
      } else if (arg.startsWith("read ")) {
        int ch = arg.substring(5).toInt() - 1; // "1"-"3" → 0-2
        if (ch >= 0 && ch <= 2) {
          float levelPct = 0.0f;
          float pfAtual = 0.0f;
          int32_t rawAtual = 0;
          bool sensorOk = false;
          bool saturado = false;
          if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(30)) == pdTRUE) {
            sensorOk = gNivel[ch].leituraOk;
            levelPct = gNivel[ch].nivelPct;
            pfAtual = gNivel[ch].pFAtual;
            rawAtual = gNivel[ch].rawAtual;
            saturado = gNivel[ch].saturado;
            xSemaphoreGive(mutexNivel);
          }
          Serial.printf("RECHARGE_LEVEL:%d,%.1f,%.4f,%ld,%d,%d\n",
                        ch, levelPct, pfAtual, (long)rawAtual, sensorOk ? 1 : 0, saturado ? 1 : 0);
        } else { Serial.println("Uso: recharge read 1|2|3"); }
      } else {
        int ch = arg.toInt() - 1; // "1"-"3" → 0-2
        if (ch >= 0 && ch <= 2) {
          RechargeCmd cmd{ RechargeCmd::START, (uint8_t)ch };
          xQueueSend(qRechargeCmd, &cmd, pdMS_TO_TICKS(100));
          Serial.printf("Iniciando recarga caneta %d...\n", ch + 1);
        } else { Serial.println("Uso: recharge 1|2|3|stop|read 1|2|3"); }
      }
    }
    else if (op == "opmode" || op.startsWith("opmode ")) {
      String arg = op.length() > 7 ? op.substring(7) : "";
      arg.trim();
      _menuOpMode(arg);
    }
    else if (op == "lock") {
      Serial.println(gBloqueado ? "DOCK_BLOCKED" : "DOCK_OK");
    }
    else if (op.startsWith("unlock ")) {
      String senha = op.substring(7); senha.trim();
      if (senha == "1234") {
        if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(500)) == pdTRUE) {
          Preferences p;
          p.begin("otb-dock", false);
          p.putUChar("locked", 0);
          p.end();
          xSemaphoreGive(mutexNVS);
        }
        gBloqueado = false;
        Serial.println("UNLOCK_OK");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
      } else {
        Serial.println("UNLOCK_ERR:wrong_password");
      }
    }
    else if (op.length() > 0) { Serial.println("Comando invalido."); }

    _mostrarMenuPrincipal();
  }
}
