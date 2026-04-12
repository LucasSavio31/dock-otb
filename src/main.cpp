// =============================================================
//  main.cpp — SmartPen DockStation OTB  V5
//  NFC + Display Nextion + Controle Bomba/Válvulas
//  SEM sensor de nível FDC1004
// =============================================================
#include <Arduino.h>
#include "shared.h"
#include "task_nfc.h"
#include "task_nextion.h"
#include "task_serial.h"
#include "task_led.h"
#include "task_controle.h"

QueueHandle_t     qTagData     = nullptr;
QueueHandle_t     qSerialCmd   = nullptr;
QueueHandle_t     qSerialResp  = nullptr;
QueueHandle_t     qControleCmd = nullptr;
SemaphoreHandle_t mutexTag     = nullptr;
TaskHandle_t      hTaskLED     = nullptr;

TagState gTag;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║   SmartPen DockStation OTB  V5       ║");
  Serial.println("╚══════════════════════════════════════╝");

  qTagData     = xQueueCreate(1, sizeof(TagEvent));
  qSerialCmd   = xQueueCreate(4, sizeof(SerialCmd));
  qSerialResp  = xQueueCreate(1, sizeof(TagEvent));
  qControleCmd = xQueueCreate(8, sizeof(ControleCmd));
  mutexTag     = xSemaphoreCreateMutex();

  configASSERT(qTagData);
  configASSERT(qSerialCmd);
  configASSERT(qSerialResp);
  configASSERT(qControleCmd);
  configASSERT(mutexTag);

  // Core 0 — NFC (protocolo I2C isolado)
  xTaskCreatePinnedToCore(taskNFC,      "NFC",      4096, nullptr, 3, nullptr,   0);

  // Core 1 — interface e controle
  xTaskCreatePinnedToCore(taskSerial,   "Serial",   4096, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskNextion,  "Nextion",  2048, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskControle, "Controle", 2048, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskLED,      "LED",      1024, nullptr, 1, &hTaskLED, 1);

  Serial.println("[Main] V5 iniciado — 5 tasks ativas.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}