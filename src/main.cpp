#include <Arduino.h>
#include "shared.h"
#include "task_nfc.h"
#include "task_nextion.h"
#include "task_serial.h"
#include "task_led.h"

// =============================================================================
// HANDLES GLOBAIS
// =============================================================================
QueueHandle_t     qTagData    = nullptr;
QueueHandle_t     qSerialCmd  = nullptr;
QueueHandle_t     qSerialResp = nullptr;
SemaphoreHandle_t mutexTag    = nullptr;
TaskHandle_t      hTaskLED    = nullptr;

TagState gTag;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.printf("\n[Main] SmartPen DockStation — FW %s\n", FW_VERSION);

  qTagData    = xQueueCreate(1, sizeof(TagEvent));
  qSerialCmd  = xQueueCreate(4, sizeof(SerialCmd));
  qSerialResp = xQueueCreate(1, sizeof(TagEvent));
  mutexTag    = xSemaphoreCreateMutex();

  configASSERT(qTagData);
  configASSERT(qSerialCmd);
  configASSERT(qSerialResp);
  configASSERT(mutexTag);

  xTaskCreatePinnedToCore(taskNFC,     "NFC",     4096, nullptr, 3, nullptr,   0);
  xTaskCreatePinnedToCore(taskSerial,  "Serial",  3072, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskNextion, "Nextion", 2048, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskLED,     "LED",     1024, nullptr, 1, &hTaskLED, 1);

  Serial.println("[Main] Tasks criadas.");
}

// =============================================================================
// LOOP  (idle)
// =============================================================================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}