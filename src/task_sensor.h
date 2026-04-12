#pragma once
// =============================================================
//  task_sensor.h — V5
//  Leitura contínua do FDC1004 via I2C (TCA9548A canal 0)
//  Publica resultado em gNivel (protegido por mutexNivel)
//  Roda no Core 0, prioridade 2
//
//  Registradores FDC1004 (datasheet SNOSCY5B):
//    0x00 MEAS1_MSB  0x01 MEAS1_LSB
//    0x08 CONF_MEAS1 0x0C FDC_CONF
//    DONE_1 = bit 3 do FDC_CONF
// =============================================================
#include "shared.h"
#include <Wire.h>

#define FDC_REG_MSB       0x00
#define FDC_REG_LSB       0x01
#define FDC_REG_CONF_M1   0x08
#define FDC_REG_FDC_CONF  0x0C
#define FDC_REG_DEV_ID    0xFF
#define FDC_DONE1         (1 << 3)

#define TCA_CH_FDC        0   // canal do FDC1004 no TCA9548A

// ── I2C helpers (usa Wire já iniciado pela taskNFC no Core 0) ──

static bool _fdcWrite(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  Wire.write((val >> 8) & 0xFF);
  Wire.write(val & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool _fdcRead(uint8_t reg, uint16_t &out) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FDC_ADDR, (uint8_t)2) != 2) return false;
  out = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

static bool _tcaSelectFDC() {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << TCA_CH_FDC);
  return Wire.endTransmission() == 0;
}

static bool _lerRawFDC(uint8_t capdac, int32_t &raw) {
  // CONF_MEAS1: CHA=CIN1(000) CHB=CAPDAC(100) CAPDAC=capdac
  uint16_t conf = 0x1000 | ((uint16_t)(capdac & 0x1F) << 5);
  if (!_fdcWrite(FDC_REG_CONF_M1, conf))   return false;

  // Dispara: RATE=100S/s + MEAS_1
  if (!_fdcWrite(FDC_REG_FDC_CONF, 0x0480)) return false;

  // Aguarda DONE_1 (bit 3)
  uint16_t fc = 0;
  uint32_t t = millis();
  do {
    vTaskDelay(pdMS_TO_TICKS(15));
    _fdcRead(FDC_REG_FDC_CONF, fc);
  } while (!(fc & FDC_DONE1) && millis() - t < 500);

  if (!(fc & FDC_DONE1)) return false;

  uint16_t msb = 0, lsb = 0;
  if (!_fdcRead(FDC_REG_MSB, msb)) return false;
  if (!_fdcRead(FDC_REG_LSB, lsb)) return false;
  raw = ((int32_t)(int16_t)msb << 8) | (lsb >> 8);
  return true;
}

// Encontra CAPDAC ideal (menor valor onde RAW não satura)
static uint8_t _encontrarCapdac() {
  for (uint8_t cap = 0; cap <= 31; cap++) {
    int32_t raw = 0;
    if (_lerRawFDC(cap, raw) && raw < 8380000 && raw > -8380000) {
      Serial.printf("[SENSOR] CAPDAC=%d (offset=%.2fpF) RAW=%ld\n",
                    cap, cap * 3.125f, raw);
      return cap;
    }
  }
  return 0;
}

// Média de N leituras
static int32_t _lerMedia(uint8_t capdac, uint8_t n = 10) {
  int64_t soma = 0; uint8_t ok = 0;
  for (uint8_t i = 0; i < n; i++) {
    int32_t r = 0;
    if (_lerRawFDC(capdac, r)) { soma += r; ok++; }
  }
  return ok ? (int32_t)(soma / ok) : 0;
}

void taskSensor(void *param) {
  // Wire já foi iniciado pela taskNFC — compartilha barramento
  // Aguarda taskNFC inicializar Wire
  vTaskDelay(pdMS_TO_TICKS(2000));

  Serial.println("[SENSOR] Iniciando FDC1004...");

  // Verifica TCA9548A
  Wire.beginTransmission(TCA_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[SENSOR] ERRO: TCA9548A nao encontrado em 0x70!");
  } else {
    Serial.println("[SENSOR] TCA9548A OK");
  }

  // Seleciona canal do FDC
  _tcaSelectFDC();

  // Verifica FDC1004
  uint16_t devId = 0;
  _fdcRead(FDC_REG_DEV_ID, devId);
  if (devId != 0x1004) {
    Serial.printf("[SENSOR] ERRO: FDC1004 nao encontrado (ID=0x%04X)\n", devId);
  } else {
    Serial.println("[SENSOR] FDC1004 OK");
  }

  // Reset
  _fdcWrite(FDC_REG_FDC_CONF, 0x8000);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Auto-calibração
  Serial.println("[SENSOR] Buscando CAPDAC...");
  uint8_t capdac = _encontrarCapdac();

  Serial.println("[SENSOR] Calibrando VAZIO — aguarde...");
  int32_t capVazio = _lerMedia(capdac, 15);
  Serial.printf("[SENSOR] RAW vazio = %ld\n", capVazio);

  // Salva calibração em gNivel
  if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(100)) == pdTRUE) {
    gNivel.capdac    = capdac;
    gNivel.capVazio  = capVazio;
    gNivel.capCheio  = capVazio; // será atualizado via Serial cmd
    gNivel.calibrado = false;
    xSemaphoreGive(mutexNivel);
  }

  Serial.println("[SENSOR] Leitura continua iniciada.");
  Serial.println("[SENSOR] Use comando 'cal' no Serial para calibrar CHEIO.");

  // Filtro média móvel
  const uint8_t FILT_N = 5;
  int32_t amostras[FILT_N] = {0};
  uint8_t idx = 0;
  bool preenchido = false;

  for (;;) {
    // Seleciona canal FDC no MUX antes de cada leitura
    _tcaSelectFDC();

    int32_t raw = 0;
    bool ok = _lerRawFDC(capdac, raw);

    // Filtro
    amostras[idx] = raw;
    idx = (idx + 1) % FILT_N;
    if (idx == 0) preenchido = true;
    uint8_t n = preenchido ? FILT_N : idx;
    int64_t soma = 0;
    for (uint8_t i = 0; i < n; i++) soma += amostras[i];
    int32_t rawFilt = (int32_t)(soma / n);

    // Calcula nível
    int nivel = 0;
    bool calibrado = false;
    int32_t capVazio2 = 0, capCheio2 = 0;

    if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(10)) == pdTRUE) {
      calibrado = gNivel.calibrado;
      capVazio2 = gNivel.capVazio;
      capCheio2 = gNivel.capCheio;
      xSemaphoreGive(mutexNivel);
    }

    if (calibrado) {
      int32_t span = capCheio2 - capVazio2;
      if (span > 0) {
        nivel = (int)(((int64_t)(rawFilt - capVazio2) * 100LL) / span);
        nivel = constrain(nivel, 0, 100);
      }
    }

    // Publica resultado
    if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(10)) == pdTRUE) {
      gNivel.rawAtual  = rawFilt;
      gNivel.nivelPct  = nivel;
      gNivel.leituraOk = ok;
      xSemaphoreGive(mutexNivel);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}