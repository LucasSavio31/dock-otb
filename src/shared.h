#pragma once
// =============================================================
//  shared.h - V5
//  Tipos, handles globais e estado compartilhado
// =============================================================
#define FIRMWARE_VERSION "V2.2.7"
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

// Bomba e valvulas
#define BOMBA_PIN    25
#define VALVULA_1    26
#define VALVULA_2    27
#define VALVULA_3    33

// Pino de detecção de violação (input-only, pull-up externo 10kΩ → 3.3V)
#define VIOLACAO_PIN 34

// Tempo do pulso de purga (ms)
#define PURGE_DURATION_MS  5000

// =========================
// ENDERECOS I2C
// =========================
#define TCA_ADDR    0x70   // TCA9548A multiplexer
#define FDC_ADDR    0x50   // FDC1004 sensor capacitivo
#define AD7747_ADDR 0x48   // AD7747  sensor capacitivo (AD0=GND)

// =========================
// ESTRUTURA DE DADOS DA TAG
// =========================
// Cor do cartucho/caneta (gravada na página 5 byte 1 da tag NTAG2xx)
// 0=desconhecida  1=vermelho  2=azul  3=amarelo
enum TagCor : uint8_t { COR_DESCONHECIDA = 0, COR_VERMELHO = 1, COR_AZUL = 2, COR_AMARELO = 3 };

struct TagData {
  uint16_t vida;
  uint16_t ciclos;
  uint8_t  status;
  TagCor   cor;      // página 5 byte 1
  char     serial[17];
  char     id[17];
};

// =========================
// EVENTO PUBLICADO PELA taskNFC
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
  uint8_t readerIdx;  // leitor que gerou o evento (0-5)
};

// =========================
// COMANDO taskSerial -> taskNFC
// =========================
struct SerialCmd {
  enum Type : uint8_t {
    CMD_IDENTIFICAR,
    CMD_LER,
    CMD_GRAVAR,
    CMD_RESETAR,
  } type;
  uint8_t readerIdx;  // 0-5: NFC #1..#6
  TagData payload;
};

// =========================
// COMANDO DE CONTROLE taskSerial -> taskAtuadores
// =========================
struct ControleCmd {
  enum Type : uint8_t {
    VALVULA_ON,
    VALVULA_OFF,
    BOMBA_ON,
    BOMBA_OFF,
    BOMBA_DUTY,
  } type;
  uint8_t payload;  // numero da valvula (1-3) ou duty % (0-100)
};

// =========================
// COMANDO DE ATUADOR taskNextion -> taskAtuadores
// (botoes da pagina configs)
// =========================
struct ActCmd {
  enum Type : uint8_t {
    ACT_PURGAR,     // Liga bomba 100% por PURGE_DURATION_MS
    ACT_VALVULA_1,  // Toggle valvula 1
    ACT_VALVULA_2,  // Toggle valvula 2
    ACT_VALVULA_3,  // Toggle valvula 3
  } type;
};

// =========================
// COMANDO DE RECARGA taskSerial/taskRecarga
// =========================
struct RechargeCmd {
  enum Type : uint8_t { START, STOP } type;
  uint8_t channel; // 0-2
};

// Número da página Nextion da tela de recarga em andamento
#define NEXTION_PAGE_ANAM_REC  7

struct RechargeInfo {
  enum Status : uint8_t { IDLE, RUNNING, TAPERING, DONE, TIMEOUT, SATURATED, SENSOR_ERR, DETECTING, ABORTED } status;
  uint8_t  channel;
  float    levelPct;
  uint8_t  dutyPct;
  uint32_t elapsedMs;
  // Dados exibidos na página anam_rec (tela 7) durante a recarga
  char     penId[17];
  char     penSerial[17];
  uint16_t penCiclos;
  char     cartId[17];
  char     cartSerial[17];
};
extern QueueHandle_t     qRechargeCmd;
extern RechargeInfo      gRecharge;
extern SemaphoreHandle_t mutexRecharge;
extern SemaphoreHandle_t mutexNVS;

// =========================
// LOG / BANCO DE EVENTOS
// =========================
enum LogSeverity : uint8_t {
  LOG_INFO = 0,
  LOG_SUCCESS,
  LOG_WARN,
  LOG_ERROR
};

struct LogEvent {
  char        origin[12];
  char        type[16];
  char        description[64];
  LogSeverity severity;
};

struct StoredLogEntry {
  uint32_t    seq;
  uint32_t    bootId;
  uint32_t    uptimeSec;
  uint32_t    epochSec;
  uint8_t     severity;
  char        origin[12];
  char        type[16];
  char        description[64];
};

constexpr uint16_t LOGDB_CAPACITY = 100;

extern QueueHandle_t     qLogEvent;
extern SemaphoreHandle_t mutexLogDb;
extern StoredLogEntry*   gLogEntries;
extern uint16_t          gLogHead;
extern uint16_t          gLogCount;
extern uint32_t          gLogBootId;
extern uint32_t          gLogNextSeq;
extern int64_t           gLogEpochBaseSec;
extern bool              gLogEpochValid;

void taskLogDb(void *param);
void logdbPublish(const char* origin, const char* type, LogSeverity severity, const char* description);
void logdbPublishf(const char* origin, const char* type, LogSeverity severity, const char* fmt, ...);
void logdbSetEpoch(uint32_t epochSec);
void logdbClear();
void logdbForceFlush();

// =========================
// SISTEMA DE ERROS
// =========================
#define ERR_COUNT  28   // indice 0 (OK) + 27 erros

#define ERR_E001    1
#define ERR_E002    2
#define ERR_E003    3
#define ERR_E004    4
#define ERR_E101    5
#define ERR_E102    6
#define ERR_E103    7
#define ERR_E104    8
#define ERR_E105    9
#define ERR_E106   10
#define ERR_E110   11
#define ERR_E111   12
#define ERR_E112   13
#define ERR_E201   14
#define ERR_E211   15
#define ERR_E212   16
#define ERR_E213   17
#define ERR_E220   18
#define ERR_E221   19
#define ERR_E222   20
#define ERR_E301   21
#define ERR_E302   22
#define ERR_E311   23
#define ERR_E312   24
#define ERR_E313   25
#define ERR_E401   26
#define ERR_E402   27

struct ErroOTB {
  uint16_t    codigo;
  const char* nivel;
  const char* descricao;
  bool        ativo;
};

// Forward declarations (definidas em task_erros.h)
void     erroSetar(uint8_t idx);
void     erroClear(uint8_t idx);
bool     erroAtivo(uint8_t idx);
uint8_t  erroGetPrimeiro();
uint16_t erroGetCodigo(uint8_t idx);
uint8_t  erroGetAtivos(uint16_t* codigos, const char** descs, uint8_t maxCount);

// =========================
// HANDLES GLOBAIS (definidos em main.cpp)
// =========================
extern QueueHandle_t     qTagData;      // TagEvent   * taskNFC -> taskSerial        (depth 4)
extern QueueHandle_t     qNextionData;  // TagEvent   * taskNFC -> taskNextion       (depth 4)
extern QueueHandle_t     qSerialCmd;    // SerialCmd  * taskSerial -> taskNFC        (depth 4)
extern QueueHandle_t     qSerialResp;   // TagEvent   * taskNFC -> taskSerial        (depth 1, overwrite)
extern QueueHandle_t     qControleCmd;  // ControleCmd* taskSerial -> taskAtuadores  (depth 4)
extern QueueHandle_t     qActCmd;       // ActCmd     * taskNextion -> taskAtuadores (depth 4)
extern SemaphoreHandle_t mutexTag;
extern SemaphoreHandle_t mutexErros;
extern SemaphoreHandle_t mutexI2C;     // protege barramento Wire (I2C)
extern SemaphoreHandle_t mutexSPI;     // protege barramento SPI (PN532)
extern TaskHandle_t      hTaskLED;
extern TaskHandle_t      hTaskI2CScan;
extern SemaphoreHandle_t semI2CScanDone;

// Sinaliza para taskNFC reconfigurar o chip principal apos _menuDiag
extern volatile bool nfcReinitPending;
// Sinaliza para taskNFC reiniciar apenas os leitores de caneta (0/1/2) após recarga concluída
extern volatile bool nfcPenReinitPending;

// Bitmask dos leitores NFC detectados no boot (bit 0 = leitor 0 / D13, bit 1 = D14, etc.)
extern volatile uint8_t nfcReaderOkMask;

// Vínculo UID → leitor de cartucho (índice 0/1/2 = leitor 4/5/6).
// uidLen == 0 significa sem vínculo (aceita qualquer tag).
// Salvo na NVS namespace "cartbind"; carregado no boot de taskNFC.
struct CartBind { uint8_t uid[7]; uint8_t uidLen; };
extern CartBind gCartBind[3];

// =========================
// ESTADO COMPARTILHADO (protegido por mutexTag)
// =========================
struct TagState {
  uint8_t  uid[7]       = {0};
  uint8_t  uidLen       = 0;
  bool     presente     = false;
  bool     cacheValido  = false;
  bool     nfcOk        = false;
  TagData  cache        = {};
};
extern TagState gTag;

// Cache por leitor NFC (protegido por mutexTag)
// Indices 0-2 = canetas, 3-5 = cartuchos
struct TagReaderState {
  TagData data     = {};
  bool    valid    = false;
  bool    presente = false;
  uint8_t uid[7]  = {};
  uint8_t uidLen  = 0;
};
extern TagReaderState gTagReaders[6];

// Canal NFC ativo (0-5), atualizado pela taskNFC
extern volatile uint8_t nfcCanalAtivo;

// Serial do dock (exibido na pagina dock_status do Nextion)
extern char gSerialDock[32];

// Duty atual da bomba 0-100 (escrito pela taskAtuadores, lido pela taskNextion)
extern volatile uint8_t gBombaDuty;

// Flag de bloqueio por violação física — persiste via NVS entre reinicializações
// Setado por taskViolacao; limpo por comando "unlock 1234" + esp_restart()
extern volatile bool gBloqueado;

// Modo de operação — persiste na NVS (namespace "otb-dock", chave "op_mode")
// OP_STANDALONE: firmware detecta caneta+cartucho e inicia recarga automático
// OP_MANUAL:     aguarda comando 'recharge N' pelo serial antes de iniciar
enum OpMode : uint8_t { OP_STANDALONE = 0, OP_MANUAL = 1 };
extern volatile OpMode gOpMode;

// Nível virtual dos cartuchos 0-100% por cor (não persistente; -5% por recarga)
// Índice = TagCor (1=vermelho, 2=azul, 3=amarelo), índice 0 não usado
extern volatile uint8_t  gCartLevel[4];
// Contagem total de recargas concluídas desde o boot (não persistente)
extern volatile uint16_t gRechargeCount;

// =========================
// CALIBRACAO DOS SENSORES DE NIVEL
// Ajuste os valores abaixo com o sensor no ar (vazio) e submerso (cheio).
// =========================
#define NIVEL_PF_VAZIO_FDC    6.15f   // pF com caneta vazia  (FDC1004) - ajustado pelo baseline atual
#define NIVEL_PF_CHEIO_FDC   15.0f    // pF com caneta cheia  (FDC1004) - calibrar!
#define NIVEL_PF_VAZIO_AD    -0.5f    // pF com caneta vazia  (AD7747)
#define NIVEL_PF_CHEIO_AD     3.5f    // pF com caneta cheia  (AD7747)  - calibrar!

// =========================
// ESTADO DO SENSOR FDC1004
// =========================
struct NivelState {
  int32_t rawAtual  = 0;
  float   pFAtual   = 0.0f;
  float   nivelPct  = 0.0f;  // 0-100%, calculado com a calibracao acima
  bool    leituraOk = false;
  bool    saturado  = false;
};
extern NivelState        gNivel[3];   // indice = canal TCA (0-2)
extern SemaphoreHandle_t mutexNivel;

// =========================
// CALIBRACAO RUNTIME POR CANAL (V5.1)
// Cada canal TCA tem calibracao independente, persistida na NVS.
// Quando valid==false, task_sensor usa as constantes NIVEL_PF_* como fallback.
// =========================
struct CalibData {
  float   vazioPf;     // pF com reservatorio vazio
  float   cheioPf;     // pF com reservatorio cheio
  uint8_t step;        // passo de variacao de nivel (%) - deve dividir 100
  bool    valid;       // true = calibracao salva e carregada da NVS
  bool    capdacEn;    // true = subtrair offset de capacitancia parasita
  float   capdacPf;    // offset a subtrair do pF bruto antes de calcular nivel
  bool    rawOffsetEn; // true = aplicar compensacao RAW antes da conversao pF
  int32_t rawOffset;   // offset de ADC raw subtraido antes de converter para pF
  uint8_t penUid[7];   // UID da caneta que originou esta calibracao (uidLen==0 = por posicao)
  uint8_t penUidLen;   // comprimento do UID (0 = calibracao por posicao, nao por caneta)
};
extern CalibData         gCalib[3];      // indice = canal TCA (0-2)
extern SemaphoreHandle_t mutexCalib;     // protege gCalib
// Setado true por taskSerial quando CAPDAC muda; taskSensor re-inicia o chip no proximo ciclo
extern volatile bool     gCalibDirty[3];

// Tipo de chip por canal — escrito exclusivamente pela taskSensor, lido por taskSerial
// Valores: 0=CHIP_NONE  1=CHIP_FDC1004  2=CHIP_AD7747
extern volatile uint8_t gChipCanalTipo[3];

// Sinaliza que o barramento I2C esta ativo — taskNFC aguarda antes de iniciar poll SPI
extern volatile bool gI2CBusy;

// =========================
// OTA — Atualização via GitHub Releases
// =========================
enum OtaCmdType : uint8_t {
  OTA_CMD_CHECK,      // verifica última release no GitHub
  OTA_CMD_UPDATE,     // baixa e grava firmware
  OTA_CMD_ROLLBACK,   // reverte para firmware anterior
  OTA_CMD_STATUS,     // imprime status atual
  OTA_CMD_WIFI_SET,   // salva credenciais WiFi na NVS
  OTA_CMD_WIFI_SCAN,  // escaneia redes disponíveis
};

struct OtaCmd {
  OtaCmdType type;
  char       ssid[64];
  char       pass[64];
};

enum OtaStateEnum : uint8_t {
  OTA_STATE_IDLE = 0,
  OTA_STATE_WIFI_CONNECTING,
  OTA_STATE_CHECKING,
  OTA_STATE_DOWNLOADING,
  OTA_STATE_FLASHING,
  OTA_STATE_DONE_OK,
  OTA_STATE_DONE_ERR,
  OTA_STATE_VALIDATING,
};

struct OtaStatus {
  OtaStateEnum state;
  char         latestVersion[16];
  char         errorMsg[96];
  int          progress;         // 0-100 durante download/flash
  bool         updateAvailable;
  bool         wifiOk;
  bool         pendingValidation;
  char         wifiSsid[64];
};

extern QueueHandle_t     qOtaCmd;
extern SemaphoreHandle_t mutexOta;
extern OtaStatus         gOtaStatus;
