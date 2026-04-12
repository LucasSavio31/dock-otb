#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// =========================
// PINOS
// =========================
#define SDA_PIN      21
#define SCL_PIN      22
#define NEXTION_RX   16
#define NEXTION_TX   17
#define LED2          2

// Bomba e válvulas (mapeamento OTB)
#define BOMBA_PIN    25
#define VALVULA_1    26
#define VALVULA_2    27
#define VALVULA_3    33

// TCA9548A
#define TCA_ADDR     0x70

// PWM bomba
#define BOMBA_PWM_CANAL  1
#define BOMBA_PWM_FREQ   5000
#define BOMBA_PWM_RES    8     // 8-bit → 0-255
#define BOMBA_PWM_DUTY   102   // 40% padrão

// =========================
// ESTRUTURA DE DADOS DA TAG
// =========================
struct TagData {
  uint16_t vida;
  uint16_t ciclos;
  uint8_t  status;
  char     serial[17];
  char     id[17];
};

// =========================
// MENSAGEM PUBLICADA PELA taskNFC
// =========================
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

// =========================
// COMANDO ENVIADO pela taskSerial para a taskNFC
// =========================
struct SerialCmd {
  enum Type : uint8_t {
    CMD_LER,
    CMD_GRAVAR,
    CMD_RESETAR,
  } type;
  TagData payload;
};

// =========================
// EVENTO DE CONTROLE (bomba/válvulas via Serial)
// =========================
struct ControleCmd {
  enum Type : uint8_t {
    BOMBA_ON,
    BOMBA_OFF,
    BOMBA_DUTY,   // payload: duty 0-100 (%)
    VALVULA_ON,   // payload: índice 1-3
    VALVULA_OFF,  // payload: índice 1-3
  } type;
  uint8_t payload;
};

// =========================
// HANDLES GLOBAIS
// =========================
extern QueueHandle_t     qTagData;
extern QueueHandle_t     qSerialCmd;
extern QueueHandle_t     qSerialResp;
extern QueueHandle_t     qControleCmd;
extern SemaphoreHandle_t mutexTag;
extern TaskHandle_t      hTaskLED;

// =========================
// ESTADO COMPARTILHADO
// =========================
struct TagState {
  uint8_t  uid[7]      = {0};
  uint8_t  uidLen      = 0;
  bool     presente    = false;
  bool     cacheValido = false;
  TagData  cache       = {};
};
extern TagState gTag;