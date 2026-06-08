#pragma once
#include "shared.h"
#include "task_erros.h"
#include <Wire.h>

#define FDC_NUM_CANAIS 3

enum ChipTipo : uint8_t {
  CHIP_NONE = 0,
  CHIP_FDC1004,
  CHIP_AD7747
};

static ChipTipo _chipCanal[FDC_NUM_CANAIS] = {
  CHIP_NONE, CHIP_NONE, CHIP_NONE
};

static uint8_t _falhasCanal[FDC_NUM_CANAIS] = {0, 0, 0};
static bool _satCanal[FDC_NUM_CANAIS] = {false, false, false};
static bool _filtroInit[FDC_NUM_CANAIS] = {false, false, false};
static float _pfFiltrado[FDC_NUM_CANAIS] = {0.0f, 0.0f, 0.0f};
static float _nivelFiltrado[FDC_NUM_CANAIS] = {0.0f, 0.0f, 0.0f};

#define FDC_FILTER_ALPHA       0.18f
#define FDC_LEVEL_DEADBAND_PCT 0.8f

static float _clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float _pfToNivelPct(ChipTipo chip, float pf) {
  float vazio = 0.0f;
  float cheio = 1.0f;

  switch (chip) {
    case CHIP_FDC1004:
      vazio = NIVEL_PF_VAZIO_FDC;
      cheio = NIVEL_PF_CHEIO_FDC;
      break;
    case CHIP_AD7747:
      vazio = NIVEL_PF_VAZIO_AD;
      cheio = NIVEL_PF_CHEIO_AD;
      break;
    default:
      return 0.0f;
  }

  const float span = cheio - vazio;
  if (fabsf(span) < 0.0001f) return 0.0f;
  return ((pf - vazio) * 100.0f) / span;
}

// Usa calibracao runtime (gCalib[ch]) quando disponivel; fallback para constantes originais.
// FDC1004: CAPDAC aplicado em hardware (subtrai do sinal) — pf ja chega compensado.
// AD7747:  CAPDAC hardware ADICIONA ao sinal (single-ended); compensacao feita aqui em software.
static float _pfToNivelPctCalib(uint8_t ch, ChipTipo chip, float pf) {
  bool  useCalib = false;
  float vazio    = 0.0f;
  float cheio    = 1.0f;
  float offset   = 0.0f;

  if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (gCalib[ch].valid) {
      vazio    = gCalib[ch].vazioPf;
      cheio    = gCalib[ch].cheioPf;
      useCalib = true;
    }
    if (chip == CHIP_AD7747 && gCalib[ch].capdacEn) {
      offset = gCalib[ch].capdacPf;
    }
    xSemaphoreGive(mutexCalib);
  }

  const float pfComp = pf - offset;

  if (useCalib) {
    const float span = cheio - vazio;
    if (fabsf(span) < 0.0001f) return 0.0f;
    return ((pfComp - vazio) * 100.0f) / span;
  }

  return _pfToNivelPct(chip, pfComp);
}

static bool _sensorSaturado(ChipTipo chip, int32_t raw, float pf, float nivelPct) {
  switch (chip) {
    case CHIP_FDC1004:
      if (raw >= 8380000 || raw <= -8380000) return true;
      if (pf >= (NIVEL_PF_CHEIO_FDC + 0.5f)) return true;
      break;
    case CHIP_AD7747:
      if (pf >= (NIVEL_PF_CHEIO_AD + 0.15f)) return true;
      if (pf <= (NIVEL_PF_VAZIO_AD - 0.5f)) return true;
      break;
    default:
      break;
  }

  return nivelPct >= 100.0f;
}

static void _aplicarFiltroNivel(uint8_t ch, ChipTipo chip, float &pf, float &nivelPct) {
  if (chip != CHIP_FDC1004) return;

  if (!_filtroInit[ch]) {
    _pfFiltrado[ch] = pf;
    _nivelFiltrado[ch] = nivelPct;
    _filtroInit[ch] = true;
  } else {
    _pfFiltrado[ch] += (pf - _pfFiltrado[ch]) * FDC_FILTER_ALPHA;

    float nivelNovo = _pfToNivelPctCalib(ch, chip, _pfFiltrado[ch]);
    if (fabsf(nivelNovo - _nivelFiltrado[ch]) >= FDC_LEVEL_DEADBAND_PCT) {
      _nivelFiltrado[ch] = nivelNovo;
    }
  }

  pf = _pfFiltrado[ch];
  nivelPct = _nivelFiltrado[ch];
}

// =============================================================
// TCA9548A
// =============================================================
static bool _tcaSel(uint8_t ch) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  if (Wire.endTransmission() == 0) return true;
  // Retry: envia 0x00 para resetar estado interno do TCA e tenta novamente
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(5));
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  return (Wire.endTransmission() == 0);
}

static void _tcaFechar() {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
}

// =============================================================
// FDC1004
// =============================================================
static bool _fdcWrite(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  Wire.write((val >> 8) & 0xFF);
  Wire.write(val & 0xFF);
  return (Wire.endTransmission() == 0);
}

static bool _fdcRead(uint8_t reg, uint16_t &out) {
  Wire.beginTransmission(FDC_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FDC_ADDR, (uint8_t)2) != 2) return false;

  out = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

// ch necessario para ler gCalib[ch].capdacPf e aplicar hardware CAPDAC
static bool _fdcInit(uint8_t ch) {
  if (!_fdcWrite(0x0C, 0x8000)) return false; // reset
  vTaskDelay(pdMS_TO_TICKS(20));

  uint16_t devId = 0;
  if (!_fdcRead(0xFF, devId)) return false;
  if (devId != 0x1004) return false;

  // CONF_MEAS1: CIN1 single-ended, CAPDAC aplicado por hardware
  // bits[15:13]=000(CIN1), bits[12:10]=100(CAPDAC ref), bits[9:5]=capdac(0-31, 3.125pF/step)
  uint8_t capdacReg = 0;
  if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (gCalib[ch].capdacEn) {
      capdacReg = (uint8_t)roundf(gCalib[ch].capdacPf / 3.125f);
      if (capdacReg > 31) capdacReg = 31;
    }
    xSemaphoreGive(mutexCalib);
  }
  uint16_t meas1 = 0x1000 | ((uint16_t)capdacReg << 5);
  if (!_fdcWrite(0x08, meas1)) return false;
  vTaskDelay(pdMS_TO_TICKS(5));

  return true;
}

static bool _fdcLer(int32_t &raw, float &pf) {
  // Dispara MEAS1
  if (!_fdcWrite(0x0C, 0x0480)) return false;

  uint16_t conf = 0;
  uint32_t t0 = millis();

  while (millis() - t0 < 100) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!_fdcRead(0x0C, conf)) return false;
    if (conf & 0x0008) break; // DONE_1
  }

  if (!(conf & 0x0008)) return false;

  uint16_t msb = 0, lsb = 0;
  if (!_fdcRead(0x00, msb)) return false;
  if (!_fdcRead(0x01, lsb)) return false;

  raw = ((int32_t)(int16_t)msb << 8) | (lsb >> 8);
  pf  = (float)raw / 524288.0f;

  return true;
}

// =============================================================
// AD7747
// =============================================================
static bool _adWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AD7747_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return (Wire.endTransmission() == 0);
}

// AD7747: CAPDAC de hardware (reg 0x0C) ADICIONA ao sinal no modo single-ended,
// comportamento oposto ao desejado para compensacao. Compensacao e feita em software
// em _pfToNivelPctCalib (subtrai capdacPf do pF lido antes de calcular nivel).
static bool _adInit(uint8_t ch) {
  (void)ch; // ch reservado; sem hardware CAPDAC no AD7747
  if (!_adWrite(0x09, 0x2C)) return false;
  if (!_adWrite(0x07, 0x80)) return false;
  if (!_adWrite(0x0A, 0x01)) return false;
  return true;
}

static bool _adLer(int32_t &raw, float &pf) {
  uint32_t t0 = millis();

  while (millis() - t0 < 300) {
    Wire.beginTransmission(AD7747_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)AD7747_ADDR, (uint8_t)1) == 1) {
      uint8_t st = Wire.read();
      if (!(st & 0x01)) break;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }

  if (millis() - t0 >= 300) return false;

  Wire.beginTransmission(AD7747_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AD7747_ADDR, (uint8_t)3) != 3) return false;

  uint32_t val = ((uint32_t)Wire.read() << 16) |
                 ((uint32_t)Wire.read() << 8)  |
                 ((uint32_t)Wire.read());

  raw = (int32_t)val;
  pf  = ((float)raw - 8388608.0f) / 8388608.0f * 4.096f;
  return true;
}

// =============================================================
// Detect
// =============================================================
static ChipTipo _detectarChip() {
  Wire.beginTransmission(FDC_ADDR);
  if (Wire.endTransmission() == 0) return CHIP_FDC1004;

  Wire.beginTransmission(AD7747_ADDR);
  if (Wire.endTransmission() == 0) return CHIP_AD7747;

  return CHIP_NONE;
}

static bool _initChip(ChipTipo chip, uint8_t ch) {
  switch (chip) {
    case CHIP_FDC1004: return _fdcInit(ch);
    case CHIP_AD7747:  return _adInit(ch);
    default:           return false;
  }
}

static bool _lerChip(ChipTipo chip, int32_t &raw, float &pf) {
  switch (chip) {
    case CHIP_FDC1004: return _fdcLer(raw, pf);
    case CHIP_AD7747:  return _adLer(raw, pf);
    default:           return false;
  }
}

// =============================================================
// TASK SENSOR
// =============================================================
void taskSensor(void *param) {
  vTaskDelay(pdMS_TO_TICKS(2500));

  // gI2CBusy engloba todos os canais — SPI nao interfere em nenhum intervalo entre canais
  gI2CBusy = true;
  if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(200)) == pdTRUE) xSemaphoreGive(mutexSPI);

  for (uint8_t ch = 0; ch < FDC_NUM_CANAIS; ch++) {
    _chipCanal[ch] = CHIP_NONE;
    _falhasCanal[ch] = 0;
    _filtroInit[ch] = false;
    _pfFiltrado[ch] = 0.0f;
    _nivelFiltrado[ch] = 0.0f;

    if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(800)) == pdTRUE) {
      if (_tcaSel(ch)) {
        _chipCanal[ch] = _detectarChip();
        if (_chipCanal[ch] != CHIP_NONE) {
          if (!_initChip(_chipCanal[ch], ch)) {
            _chipCanal[ch] = CHIP_NONE;
          }
        }
        gChipCanalTipo[ch] = (uint8_t)_chipCanal[ch];
      }
      _tcaFechar();
      xSemaphoreGive(mutexI2C);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  gI2CBusy = false;

  for (;;) {
    uint8_t tcaOkNesteCiclo = 0;  // conta canais que responderam ao TCA neste ciclo

    // gI2CBusy engloba todos os 3 canais — SPI bloqueado durante todo o ciclo de leitura
    gI2CBusy = true;
    if (xSemaphoreTake(mutexSPI, pdMS_TO_TICKS(200)) == pdTRUE) xSemaphoreGive(mutexSPI);

    for (uint8_t ch = 0; ch < FDC_NUM_CANAIS; ch++) {
      int32_t raw = 0;
      float pf = 0.0f;
      float nivelPct = 0.0f;
      bool ok = false;
      bool saturado = false;

      if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(800)) == pdTRUE) {
        if (_tcaSel(ch)) {
          tcaOkNesteCiclo++;

          // Re-init solicitado por taskSerial apos mudanca de CAPDAC
          if (gCalibDirty[ch] && _chipCanal[ch] != CHIP_NONE) {
            _initChip(_chipCanal[ch], ch);
            _filtroInit[ch] = false;
            gCalibDirty[ch] = false;
          }

          // Tenta redetectar chip se ainda nao encontrou
          if (_chipCanal[ch] == CHIP_NONE) {
            _chipCanal[ch] = _detectarChip();
            if (_chipCanal[ch] != CHIP_NONE) {
              if (!_initChip(_chipCanal[ch], ch)) {
                _chipCanal[ch] = CHIP_NONE;
              }
            }
            gChipCanalTipo[ch] = (uint8_t)_chipCanal[ch];
          }

          if (_chipCanal[ch] != CHIP_NONE) {
            ok = _lerChip(_chipCanal[ch], raw, pf);

            if (!ok) {
              _falhasCanal[ch]++;
              if (_falhasCanal[ch] >= 5) {
                Serial.printf("[Sensor] CH%d: %d falhas — chip reiniciando\n",
                              ch, (int)_falhasCanal[ch]);
                erroSetar(ERR_E211 + ch);
                _chipCanal[ch] = CHIP_NONE;
                _falhasCanal[ch] = 0;
                _filtroInit[ch] = false;
                gChipCanalTipo[ch] = 0;
              }
            } else {
              _falhasCanal[ch] = 0;
              erroClear(ERR_E211 + ch);
              nivelPct = _pfToNivelPctCalib(ch, _chipCanal[ch], pf);
              _aplicarFiltroNivel(ch, _chipCanal[ch], pf, nivelPct);
              saturado = _sensorSaturado(_chipCanal[ch], raw, pf, nivelPct);
            }
          }
        } else {
          Serial.printf("[Sensor] TCA CH%d: sem resposta\n", ch);
        }

        _tcaFechar();
        xSemaphoreGive(mutexI2C);
      }

      if (xSemaphoreTake(mutexNivel, pdMS_TO_TICKS(20)) == pdTRUE) {
        gNivel[ch].rawAtual  = raw;
        gNivel[ch].pFAtual   = pf;
        gNivel[ch].nivelPct  = ok ? _clampf(nivelPct, 0.0f, 100.0f) : 0.0f;
        gNivel[ch].leituraOk = ok;
        gNivel[ch].saturado  = ok && saturado;
        xSemaphoreGive(mutexNivel);
      }

      _satCanal[ch] = ok && saturado;
      bool anySat = false;
      for (uint8_t i = 0; i < FDC_NUM_CANAIS; i++) {
        if (_satCanal[i]) { anySat = true; break; }
      }
      if (anySat) erroSetar(ERR_E221);
      else        erroClear(ERR_E221);

      vTaskDelay(pdMS_TO_TICKS(100));
    }

    gI2CBusy = false;  // SPI liberado — NFC pode fazer polls durante a pausa

    // Verifica saude do TCA apos ciclo completo
    if (tcaOkNesteCiclo == 0) {
      // Nenhum canal respondeu ao TCA9548A — sinaliza falha de I2C
      erroSetar(ERR_E201);
      Serial.println("[Sensor] TCA9548A: sem resposta em todos os canais");
    } else {
      erroClear(ERR_E201);
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
