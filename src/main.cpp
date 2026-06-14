

#include <Arduino.h>
#include <stdlib.h>
#include <Wire.h>
#include "shared.h"
#include "task_erros.h"
#include "task_nfc.h"
#include "task_nextion.h"
#include "task_serial.h"
#include "task_led.h"
#include "task_atuadores.h"
#include "task_sensor.h"
#include "task_i2c_scan.h"
#include "task_nvs.h"
#include "task_recarga.h"
#include "task_logdb.h"
#include "task_ota.h"
#include "task_violacao.h"

// =========================
// HANDLES GLOBAIS
// =========================
QueueHandle_t     qTagData     = nullptr;
QueueHandle_t     qNextionData = nullptr;
QueueHandle_t     qSerialCmd   = nullptr;
QueueHandle_t     qSerialResp  = nullptr;
QueueHandle_t     qControleCmd = nullptr;
QueueHandle_t     qActCmd      = nullptr;
QueueHandle_t     qRechargeCmd = nullptr;
QueueHandle_t     qLogEvent    = nullptr;
SemaphoreHandle_t mutexTag     = nullptr;
SemaphoreHandle_t mutexErros   = nullptr;
SemaphoreHandle_t mutexI2C     = nullptr;
SemaphoreHandle_t mutexSPI     = nullptr;
SemaphoreHandle_t mutexLogDb   = nullptr;
SemaphoreHandle_t mutexNVS     = nullptr;
TaskHandle_t      hTaskLED     = nullptr;
TaskHandle_t      hTaskI2CScan = nullptr;
SemaphoreHandle_t semI2CScanDone = nullptr;
volatile bool     nfcReinitPending = false;
volatile bool     gBloqueado       = false;
volatile uint8_t  nfcReaderOkMask  = 0;
volatile bool     gI2CBusy         = false;

TagState         gTag;
TagReaderState   gTagReaders[6];
volatile uint8_t  gBombaDuty     = 0;
volatile uint8_t  gCartLevel[4]  = {100, 100, 100, 100}; // [0] unused; [1-3] = vermelho/azul/amarelo
volatile uint16_t gRechargeCount = 0;
NivelState       gNivel[3];
SemaphoreHandle_t mutexNivel = nullptr;
volatile uint8_t nfcCanalAtivo = 0;
char             gSerialDock[32] = "OTB202601";  // ajuste conforme necessário

CalibData         gCalib[3];
SemaphoreHandle_t mutexCalib        = nullptr;
volatile uint8_t  gChipCanalTipo[3] = {0, 0, 0};
volatile bool     gCalibDirty[3]    = {false, false, false};
StoredLogEntry*   gLogEntries      = nullptr;
uint16_t          gLogHead         = 0;
uint16_t          gLogCount        = 0;
uint32_t          gLogBootId       = 0;
uint32_t          gLogNextSeq      = 1;
int64_t           gLogEpochBaseSec = 0;
bool              gLogEpochValid   = false;

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);

  // Wire inicializado aqui, antes das tasks, para evitar race condition entre
  // taskNFC (Core 0) e taskNextion (Core 1) que chamavam Wire.begin() simultaneamente
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(10000);   // 10 kHz — velocidade minima pratica do ESP32 I2C
  Wire.setTimeOut(100);   // 100 ms — a 10 kHz buffer de 32 bytes leva ~29 ms; margem segura

  qTagData     = xQueueCreate(4, sizeof(TagEvent));
  qNextionData = xQueueCreate(4, sizeof(TagEvent));
  qSerialCmd   = xQueueCreate(4, sizeof(SerialCmd));
  qSerialResp  = xQueueCreate(1, sizeof(TagEvent));
  qControleCmd = xQueueCreate(4, sizeof(ControleCmd));
  qActCmd      = xQueueCreate(4, sizeof(ActCmd));
  qLogEvent    = xQueueCreate(16, sizeof(LogEvent));
  gLogEntries  = static_cast<StoredLogEntry*>(calloc(LOGDB_CAPACITY, sizeof(StoredLogEntry)));
  mutexTag     = xSemaphoreCreateMutex();
  mutexErros   = xSemaphoreCreateMutex();
  mutexI2C     = xSemaphoreCreateMutex();
  mutexSPI     = xSemaphoreCreateMutex();
  mutexLogDb   = xSemaphoreCreateMutex();
  mutexNVS     = xSemaphoreCreateMutex();
  mutexNivel      = xSemaphoreCreateMutex();
  mutexCalib      = xSemaphoreCreateMutex();
  mutexRecharge   = xSemaphoreCreateMutex();
  semI2CScanDone  = xSemaphoreCreateBinary();
  qRechargeCmd    = xQueueCreate(4, sizeof(RechargeCmd));
  qOtaCmd         = xQueueCreate(4, sizeof(OtaCmd));
  mutexOta        = xSemaphoreCreateMutex();

  configASSERT(qTagData);
  configASSERT(qNextionData);
  configASSERT(qSerialCmd);
  configASSERT(qSerialResp);
  configASSERT(qControleCmd);
  configASSERT(qActCmd);
  configASSERT(qLogEvent);
  configASSERT(gLogEntries);
  configASSERT(mutexTag);
  configASSERT(mutexErros);
  configASSERT(mutexI2C);
  configASSERT(mutexSPI);
  configASSERT(mutexNVS);
  configASSERT(mutexLogDb);
  configASSERT(mutexNivel);
  configASSERT(mutexCalib);
  configASSERT(semI2CScanDone);
  configASSERT(qRechargeCmd);
  configASSERT(mutexRecharge);
  configASSERT(qOtaCmd);
  configASSERT(mutexOta);

  // Core 0: NFC (prio 3) + Sensor (prio 2) + Atuadores (prio 2)
  // Core 1: Nextion (prio 2) + Serial (prio 2) + Erros (prio 1) + LED (prio 1) + I2CScan (prio 1)
  xTaskCreatePinnedToCore(taskNFC,       "NFC",       4096, nullptr, 3, nullptr,   0);
  xTaskCreatePinnedToCore(taskSensor,    "Sensor",    2048, nullptr, 2, nullptr,   0);
  xTaskCreatePinnedToCore(taskAtuadores, "Atuadores", 2048, nullptr, 2, nullptr,   0);
  xTaskCreatePinnedToCore(taskNextion,   "Nextion",   4096, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskSerial,    "Serial",    4096, nullptr, 2, nullptr,   1);
  xTaskCreatePinnedToCore(taskErros,     "Erros",     2048, nullptr, 1, nullptr,   1);
  xTaskCreatePinnedToCore(taskLED,       "LED",       2048, nullptr, 1, &hTaskLED,      1);
  xTaskCreatePinnedToCore(taskI2CScan,   "I2CScan",   2048, nullptr, 1, &hTaskI2CScan,  1);
  xTaskCreatePinnedToCore(taskNVS,       "NVS",       2048, nullptr, 1, nullptr,         1);
  xTaskCreatePinnedToCore(taskRecarga,   "Recarga",   3072, nullptr, 2, nullptr,         0);
  xTaskCreatePinnedToCore(taskLogDb,     "LogDb",     4096, nullptr, 1, nullptr,         1);
  xTaskCreatePinnedToCore(taskOTA,           "OTA",         12288, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(taskOtaAutoCheck, "OtaAutoCheck", 10240, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(taskViolacao,     "Violacao",      2048, nullptr, 2, nullptr, 1);

  Serial.println("[Main] OTB DockStation V5 — tasks criadas.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
