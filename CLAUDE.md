# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Project Overview

ESP32 firmware for the **OTB DockStation** — an industrial system that identifies, monitors, and automatically recharges SmartPens (industrial pens) via NFC tags. Built with PlatformIO + Arduino framework + FreeRTOS.

---

## Build & Flash Commands

```bash
# Compile
pio run

# Compile + Flash
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor

# Flash + monitor in sequence
pio run --target upload && pio device monitor
```

**Debug level:** `CORE_DEBUG_LEVEL=0` in `platformio.ini` (set to 5 for verbose ESP-IDF logs).

---

## FreeRTOS Architecture

8 tasks split across 2 cores. Never block Core 0 tasks with I/O — they handle time-critical SPI/I2C polling.

| Core | Task | Priority | Stack | Responsibility |
|---|---|---|---|---|
| 0 | taskNFC | 3 | 4096 | SPI polling PN532, NTAG read/write |
| 0 | taskSensor | 2 | 2048 | I2C capacitive sensors (FDC1004/AD7747) |
| 0 | taskAtuadores | 2 | 2048 | Pump PWM (LEDC) + solenoid valves |
| 1 | taskNextion | 2 | 3072 | Nextion HMI display over Serial2 |
| 1 | taskSerial | 2 | 4096 | Interactive serial menu |
| 1 | taskErros | 1 | 2048 | Heap monitoring every 5s |
| 1 | taskLED | 1 | 2048 | Onboard LED feedback |
| 1 | taskI2CScan | 1 | 2048 | On-demand I2C bus scanner |

### Inter-Task Communication (all defined in `main.cpp`, declared in `shared.h`)

| Queue/Semaphore | Type | Direction | Notes |
|---|---|---|---|
| `qTagData` | TagEvent | taskNFC → taskSerial | depth 4 |
| `qNextionData` | TagEvent | taskNFC → taskNextion | depth 4 |
| `qSerialCmd` | SerialCmd | taskSerial → taskNFC | depth 4 |
| `qSerialResp` | TagEvent | taskNFC → taskSerial | depth 1, overwrite |
| `qControleCmd` | ControleCmd | taskSerial → taskAtuadores | depth 4 |
| `qActCmd` | ActCmd | taskNextion → taskAtuadores | depth 4 |
| `mutexTag` | Mutex | — | protects `gTag` global state |
| `mutexNivel` | Mutex | — | protects `gNivel[3]` sensor readings |
| `mutexI2C` | Mutex | — | protects Wire bus |
| `mutexSPI` | Mutex | — | protects SPI bus (PN532) |
| `mutexErros` | Mutex | — | protects `gErros[]` error table |
| `semI2CScanDone` | Binary sem | taskI2CScan → taskSerial | scan completion signal |

**Critical rule:** Always take `mutexSPI` before touching PN532 from any task other than taskNFC. Use `mutexI2C` before any `Wire.*` call. Timeouts: mutexSPI 500ms, mutexI2C 800ms.

---

## Source File Map (`src/`)

- **`shared.h`** — all pin defines, struct definitions (`TagData`, `TagEvent`, `SerialCmd`, `ControleCmd`, `ActCmd`, `ErroOTB`, `TagState`, `NivelState`), extern declarations for all globals, error code defines (`ERR_E001`…`ERR_E402`)
- **`task_nfc.h`** — PN532 SPI driver; NTAG2xx read/write (pages 4–13); 6-reader boot check; event publishing via `_publicarEvento()`
- **`task_sensor.h`** — FDC1004 + AD7747 auto-detection per TCA9548A channel; reads `gNivel[3]`; marks `ERR_E211-E213` per channel
- **`task_atuadores.h`** — LEDC PWM on BOMBA_PIN (D25); GPIO valves D26/D27/D33; handles purge timer; `ERR_E302` if pump runs without open valve
- **`task_nextion.h`** — Serial2 at 9600 baud; binary touch packets `0x65 page comp event 0xFF 0xFF 0xFF`; text commands `HOME:ABRIU`; 2s refresh cycle
- **`task_serial.h`** — Interactive menu: `pen`, `cart`, `valv`, `pump`, `diag`, `dock`, `sensor`, `erros`, `i2cscan`
- **`task_erros.h`** — Error table `gErros[28]`; thread-safe `erroSetar(idx)` / `erroClear(idx)` / `erroGetAtivos()`
- **`task_led.h`** — `xTaskNotify`: 0=off, 1=tag present, 2=blink 2x fast
- **`task_i2c_scan.h`** — Triggered by `xTaskNotifyGive(hTaskI2CScan)`; scans all 8 TCA channels + main bus

---

## NFC Tag Layout (NTAG2xx)

| Page | Content |
|---|---|
| 4 | `vida` (uint16 LE bytes 0-1) + `ciclos` (uint16 LE bytes 2-3) |
| 5 | `status` (byte 0), 3 padding bytes |
| 6–9 | `serial[16]` (4 bytes/page) |
| 10–13 | `id[16]` (4 bytes/page) |

Reader 0 (CS=D13) is the primary reader — firmware halts in boot loop if it's absent.

---

## Error Code Ranges

| Range | Subsystem |
|---|---|
| E001–E004 | System (FreeRTOS, NVS, heap, WDT) |
| E101–E106 | NFC readers 1–6 |
| E110–E112 | NFC tag operations |
| E201 | TCA9548A MUX missing |
| E211–E213 | Level sensors CH0–CH2 |
| E220–E222 | FDC1004 specific |
| E301–E313 | Actuators (timeout, pump, valves) |
| E401–E402 | Nextion display |

---

## Dashboard (`dashboard/`)

Single-file web app (`index.html`) using the **Web Serial API** — requires Chrome or Edge 89+, does not work in Firefox.

**To use:** open `dashboard/index.html` directly in Chrome/Edge. Click **Conectar** to select the ESP32 COM port.

**Dashboard pages:**
- Overview — memory gauges, active errors summary, sensor readings
- Erros — full error code list with descriptions
- Sistema Dockstation — ESP32 system info (heap, flash, uptime, tasks)
- NFC / Tags — read/write/reset tags, select reader 1–6
- Atuadores — valve toggles, pump PWM slider, purge button
- Sensores — live FDC1004/AD7747 readings with bar graphs + Chart.js history
- Diagnóstico — triggers full hardware diagnostic via serial
- I2C Scanner — triggers `i2cscan` menu command, displays results
- Console — raw serial terminal

**Dashboard communication model:** The dashboard parses serial output from the ESP32 interactive menu. It sends text commands (same as the serial menu: `sensor`, `erros`, `dock`, `diag`, `i2cscan`, valve/pump commands) and parses structured text responses. There is no custom binary protocol between firmware and dashboard.

**i18n system:**
- `i18n.js` — generated file, contains `I18N_DICT` for PT-BR/EN/ES; do not edit manually
- `gen_i18n.js` — Node.js script that regenerates `i18n.js`: `node gen_i18n.js`
- `strings.json` — inventory of UI strings by line number (reference only)
- Add new translatable strings to the `dict` object in `gen_i18n.js`, then run it

---

## Key Design Constraints

- **D12 must never be used** — it is a boot-critical strapping pin.
- **D4, D5, D15, D2** are boot-sensitive; they are used as NFC CS pins but need 10kΩ pull-ups for stable boot.
- `nfcReinitPending = true` must be set after any external `nfc.begin()` call (e.g., diagnostic) so taskNFC reconfigures the primary reader.
- The sensor task supports both FDC1004 (addr `0x50`) and AD7747 (addr `0x48`) — auto-detected per TCA channel on boot and on read failure recovery.
- Pump (`ERR_E302`): firing the pump with no valve open is flagged as an error. taskAtuadores checks `stVal[1..3]` before setting duty > 0.
