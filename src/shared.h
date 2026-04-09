#pragma once
// =============================================================================
// shared.h — v3.1
// 1 leitor PN532 via SPI, CS no D13.
// Structs idênticas à V3 — sem breaking changes em task_serial / task_nextion.
// =============================================================================
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// =============================================================================
// VERSÃO
// =============================================================================
#define FW_VERSION "3.1.0"

// =============================================================================
// PINOS — SPI  (PN532 único, CS = D13)
// Fonte: Mapeamento_de_Pinos_ESP32.xlsx — aba "Mapa Pinos Dock"
//
//   PN532       ESP32
//   SCK    →    D18
//   MISO   →    D19
//   MOSI   →    D23
//   CS     →    D13   (CS_NFC_1)
//   VCC    →    3.3V
//   GND    →    GND
// =============================================================================
#define SPI_SCK      18
#define SPI_MISO     19
#define SPI_MOSI     23
#define NFC_CS       13

// =============================================================================
// PINOS — I2C  (TCA9548A + FDC1004 — mantidos para futuro)
// =============================================================================
#define SDA_PIN      21
#define SCL_PIN      22

// =============================================================================
// PINOS — NEXTION  (UART2)
// =============================================================================
#define NEXTION_RX   16
#define NEXTION_TX   17

// =============================================================================
// PINOS — ATUADORES
// =============================================================================
#define PWM_BOMBA    25
#define VALVULA_1    26
#define VALVULA_2    27
#define VALVULA_3    33

// =============================================================================
// LED de status
// =============================================================================
#define LED2          2

// =============================================================================
// ESTRUTURAS DE DADOS  (iguais à V3 — sem breaking changes)
// =============================================================================
struct TagData {
  uint16_t vida;
  uint16_t ciclos;
  uint8_t  status;
  char     serial[17];
  char     id[17];
};

struct TagEvent {
  enum Type : uint8_t {
    TAG_PRESENTE,
    TAG_REMOVIDA,
    TAG_LIDA,
    TAG_GRAVADA,
    TAG_RESETADA,
    TAG_ERRO,
  } type;
  TagData data;
  uint8_t uid[7];
  uint8_t uidLen;
};

struct SerialCmd {
  enum Type : uint8_t {
    CMD_LER,
    CMD_GRAVAR,
    CMD_RESETAR,
  } type;
  TagData payload;
};

// =============================================================================
// HANDLES GLOBAIS  (definidos em main.cpp)
// =============================================================================
extern QueueHandle_t     qTagData;
extern QueueHandle_t     qSerialCmd;
extern QueueHandle_t     qSerialResp;
extern SemaphoreHandle_t mutexTag;
extern TaskHandle_t      hTaskLED;

// =============================================================================
// ESTADO COMPARTILHADO  (protegido por mutexTag)
// =============================================================================
struct TagState {
  uint8_t  uid[7]      = {0};
  uint8_t  uidLen      = 0;
  bool     presente    = false;
  bool     cacheValido = false;
  TagData  cache       = {};
};
extern TagState gTag;