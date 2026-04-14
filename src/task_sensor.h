#pragma once
// =============================================================
//  task_sensor.h — V5
//  Leitura FDC1004 via TCA9548A canal 0
//  Publica RAW e pF em gNivel (protegido por mutexNivel)
//  Roda no Core 0, prioridade 2
//
//  FDC1004 addr=0x50 | TCA9548A addr=0x70 canal CH0
//  CONF_MEAS1: CIN1(000) vs CAPDAC(100) → 0x1000
//  FDC_CONF:   RATE=100S/s + MEAS_1     → 0x0480
//  DONE_1 = bit 3 do FDC_CONF
// =============================================================
#include "shared.h"
#include <Wire.h>

#define FDC_REG_MSB       0x00
#define FDC_REG_LSB       0x01
#define FDC_REG_CONF_M1   0x08
#define FDC_REG_FDC_CONF  0x0C
#define FDC_REG_DEV_ID    0xFF
#define FDC_DONE1         (1 << 3)
#define TCA_CH_FDC        0

// ── I2C helpers ───────────────────────────────────────────────

static bool _sWrite(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  Wire.write((val >> 8) & 0xFF);
  Wire.write(val & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool _sRead(uint8_t reg, uint16_t &out) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FDC_ADDR, (uint8_t)2) != 2) return false;
  out = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

static bool _tcaFDC() {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << TCA_CH_FDC);
  return Wire.endTransmission() == 0;
}

// Lê RAW do FDC1004 — sem verificar saturação, retorna sempre o valor
static bool _lerRawFDC(int32_t &raw) {
  if (!_sWrite(FDC_REG_CONF_M1,  0x1000)) return false; // CIN1 vs CAPDAC
  if (!_sWrite(FDC_REG_FDC_CONF, 0x0480)) return false; // RATE=100S/s MEAS_1

  uint16_t fc = 0;
  uint32_t t = millis();
  do {
    vTaskDelay(pdMS_TO_TICKS(15));
    _sRead(FDC_REG_FDC_CONF, fc);
  } while (!(fc & FDC_DONE1) && millis() - t < 500);

  if (!(fc & FDC_DONE1)) return false;

  uint16_t msb = 0, lsb = 0;
  if (!_sRead(FDC_REG_MSB, msb)) return false;
  if (!_sRead(FDC_REG_LSB, lsb)) return false;

  // Resultado 24 bits com sinal
  raw = ((int32_t)(int16_t)msb << 8) | (lsb >> 8);
  return true;
}

// =========================
// TASK SENSOR
// Core 0, prioridade 2
// =========================
void taskSensor(void *param) {
  // Wire já iniciado pela taskNFC — aguarda estabilizar
  vTaskDelay(pdMS_TO_TICKS(2000));

  Serial.println("[SENSOR] Iniciando FDC1004...");

  // Verifica TCA9548A
  Wire.beginTransmission(TCA_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[SENSOR] ERRO: TCA9548A nao encontrado (0x70)");
  } else {
    Serial.println("[SENSOR] TCA9548A OK");
  }

  // Seleciona canal e verifica FDC1004
  _tcaFDC();
  uint16_t devId = 0;
  _sRead(FDC_REG_DEV_ID, devId);
  if (devId == 0x1004) {
    Serial.println("[SENSOR] FDC1004 OK (0x50)");
  } else {
    Serial.printf("[SENSOR] ERRO: FDC1004 nao encontrado (ID=0x%04X)\n", devId);
  }

  // Reset
  _sWrite(FDC_REG_FDC_CONF, 0x8000);
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.println("[SENSOR] Leitura continua iniciada.");

  for (;;) {
    _tcaFDC(); // seleciona canal antes de cada leitura

    int32_t raw = 0;
    bool ok = _lerRawFDC(raw);

    // Converte para pF: Capacitancia(pF) = raw / 2^19
    float pf = (float)raw / 524288.0f;

    // Publica resultado
    if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(10)) == pdTRUE) {
      gNivel.rawAtual  = raw;
      gNivel.pFAtual   = pf;
      gNivel.leituraOk = ok;
      xSemaphoreGive(mutexNivel);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}