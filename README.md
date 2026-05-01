# 🖊️ DockStation OTB — Firmware V4

Sistema embarcado com **ESP32** para identificação, monitoramento e recarga automática de canetas industriais (SmartPen).

---

## 🚀 Versão atual: V4

| Versão | Descrição |
|--------|-----------|
| V3 | Fix race condition — display não atualizava sem remover a tag |
| **V4** | Migração I²C → **SPI** · NFC Cartucho · Display Nextion |

---

## 📐 Diagrama de Pinagem — ESP32

### 🔵 SPI — Leitores NFC PN532

```
ESP32               PN532 (módulo)
─────────────────────────────────────────
D18  (SPI SCK)  ──► SCK   (clock)
D19  (SPI MISO) ◄── MISO  (dados entrada)
D23  (SPI MOSI) ──► MOSI  (dados saída)
─────────────────────────────────────────
D13  (CS_NFC_1) ──► CS    Leitor 1  ✅ uso atual (V4)
D14  (CS_NFC_2) ──► CS    Leitor 2
D4   (CS_NFC_3) ──► CS    Leitor 3  ⚠️  pino de boot
D5   (CS_NFC_4) ──► CS    Leitor 4  ⚠️  pino de boot
D15  (CS_NFC_5) ──► CS    Leitor 5  ⚠️  pino de boot
D2   (CS_NFC_6) ──► CS    Leitor 6  ⚠️  pino de boot / LED onboard
─────────────────────────────────────────
3.3V            ──► VCC
GND             ──► GND
```

> ⚠️ **Recomendação:** colocar pull-up de **10kΩ** nos pinos CS (especialmente D4, D5, D15, D2) para estabilidade no boot.

---

### 🟡 I²C — Sensor de Nível e MUX

```
ESP32               Periférico
─────────────────────────────────────────
D21  (SDA)      ──► TCA9548A (MUX I²C)
D22  (SCL)      ──► TCA9548A (MUX I²C)
─────────────────────────────────────────
TCA9548A CH0    ──► FDC1004 — Caneta 1
TCA9548A CH1    ──► FDC1004 — Caneta 2
TCA9548A CH2    ──► FDC1004 — Caneta 3
```

---

### 🟢 UART2 — Display Nextion

```
ESP32               Nextion 4.3"
─────────────────────────────────────────
D17  (TX2)      ──► RX
D16  (RX2)      ◄── TX
```

---

### 🔴 Atuadores — Bomba e Válvulas

```
ESP32               ULN2003 / Carga
─────────────────────────────────────────
D25  (PWM)      ──► Bomba       (PWM)
D26             ──► Válvula 1   (ON/OFF)
D27             ──► Válvula 2   (ON/OFF)
D33             ──► Válvula 3   (ON/OFF)
```

---

### ⚪ Pinos Reserva / Entrada

```
Pino    Função              Obs
──────────────────────────────────────────
D32     Reserva             Futuro uso
D34     Entrada futura      Somente entrada (sem pull-up interno)
D35     Entrada futura      Somente entrada (sem pull-up interno)
RX0     Serial USB          Deixar livre — programação/debug
TX0     Serial USB          Deixar livre — programação/debug
D12     ❌ NÃO USAR         Pino crítico de boot
```

---

### 📋 Tabela Completa de Pinos

| Pino ESP32 | Função | Periférico | Direção | Observação |
|---|---|---|---|---|
| D18 | SPI SCK | PN532 (todos) | Saída | Clock SPI compartilhado |
| D19 | SPI MISO | PN532 (todos) | Entrada | MISO compartilhado |
| D23 | SPI MOSI | PN532 (todos) | Saída | MOSI compartilhado |
| D13 | CS_NFC_1 | PN532 Leitor 1 | Saída | **Ativo na V4** |
| D14 | CS_NFC_2 | PN532 Leitor 2 | Saída | Pull-up 10kΩ |
| D4 | CS_NFC_3 | PN532 Leitor 3 | Saída | ⚠️ Boot · Pull-up 10kΩ |
| D5 | CS_NFC_4 | PN532 Leitor 4 | Saída | ⚠️ Boot · Pull-up 10kΩ |
| D15 | CS_NFC_5 | PN532 Leitor 5 | Saída | ⚠️ Boot · Pull-up 10kΩ |
| D2 | CS_NFC_6 | PN532 Leitor 6 | Saída | ⚠️ Boot · LED onboard |
| D21 | I²C SDA | TCA9548A + FDC1004 | Bidirecional | Barramento I²C principal |
| D22 | I²C SCL | TCA9548A + FDC1004 | Saída | Barramento I²C principal |
| D17 | UART2 TX | Nextion RX | Saída | Display HMI |
| D16 | UART2 RX | Nextion TX | Entrada | Display HMI |
| D25 | PWM_BOMBA | ULN2003 | Saída | PWM da bomba |
| D26 | VALVULA_1 | ULN2003 | Saída | Válvula ON/OFF 1 |
| D27 | VALVULA_2 | ULN2003 | Saída | Válvula ON/OFF 2 |
| D33 | VALVULA_3 | ULN2003 | Saída | Válvula ON/OFF 3 |
| D32 | Reserva | — | E/S | Livre |
| D34 | Entrada futura | Sensor/sinal | Entrada | Somente entrada |
| D35 | Entrada futura | Sensor/sinal | Entrada | Somente entrada |
| RX0 | Serial USB | Debug | Entrada | Deixar livre |
| TX0 | Serial USB | Debug | Saída | Deixar livre |
| D12 | ❌ NÃO USAR | — | — | Pino crítico de boot |

---

## 🧠 Arquitetura de Software

```
Core 0                     Core 1
──────────────────         ──────────────────────────
taskNFC  (prio 3)          taskSerial  (prio 2)
  │ SPI + PN532              │ Menu serial interativo
  │ polling + cmds           │
  ▼                          ▼
  qTagData ──────────►  taskNextion (prio 2)
  qSerialResp ────────►   │ Atualiza display Nextion
  mutexTag (gTag)          │
                           taskLED    (prio 1)
                             LED onboard feedback
```

---

## 🔧 Dependências (platformio.ini)

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
lib_deps  =
    adafruit/Adafruit PN532
```

---

## 📁 Estrutura do Projeto

```
dock-otb/
├── src/
│   ├── main.cpp
│   ├── shared.h          ← defines de pinos, structs, handles globais
│   ├── task_nfc.h        ← polling SPI, leitura/gravação NTAG
│   ├── task_nextion.h    ← atualização do display HMI
│   ├── task_serial.h     ← menu serial (ler / gravar / resetar)
│   └── task_led.h        ← feedback LED onboard
└── Mapeamento_de_Pinos_ESP32.xlsx
```

---

## ⚡ Lógica de Recarga Automática

- Nível ≤ **40%** → abre tela de recarga, aguarda 2s, inicia recarga
- Nível ≥ **80%** → para recarga, exibe "Recarga Concluída" com ID e nível
- Após 2s → retorna para tela inicial

---

## Desktop App

O dashboard web também pode ser empacotado como aplicativo Windows (`.exe`) via Electron.

Arquivos principais:

- `package.json`
- `electron/main.js`
- `dashboard/index.html`

Comandos:

```powershell
npm.cmd install
npm.cmd start
npm.cmd run pack
npm.cmd run dist
```

Saída esperada:

- executável pronto em `dist-electron/win-unpacked/`
- instalador e app portátil em `dist-electron/`

Observações:

- o app usa um protocolo local seguro `app://local` para manter compatibilidade com Web Serial;
- ao pedir conexão, o Electron abre a seleção da porta serial disponível para a dock.
- nesta máquina, a geração do `.exe` em pasta (`win-unpacked`) funcionou; o instalador portátil pode depender de permissões extras de symlink do Windows.
