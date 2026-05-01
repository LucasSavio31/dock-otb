#pragma once
#include "shared.h"
#include <Wire.h>

static const char* _i2cNomeDispositivo(uint8_t addr) {
  switch (addr) {
    case 0x3C: case 0x3D: return "OLED SSD1306";
    case 0x40:             return "INA219/PCA9685";
    case 0x48: case 0x49:
    case 0x4A: case 0x4B: return "AD7747/ADS1115/ADS1015";
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57: return "EEPROM/FDC1004";
    case 0x68: case 0x69: return "MPU6050/DS3231";
    case 0x70:             return "TCA9548A (MUX I2C)";
    case 0x76: case 0x77: return "BME280/BMP280";
    default:               return nullptr;
  }
}

// ignoreAddr = 0x00 -> nao ignora nenhum
static uint8_t _scanBus(const char* prefixo, uint8_t ignoreAddr = 0x00) {
  uint8_t found = 0;

  for (uint8_t addr = 0x01; addr < 0x80; addr++) {
    if (ignoreAddr != 0x00 && addr == ignoreAddr) {
      continue;
    }

    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      const char* nome = _i2cNomeDispositivo(addr);
      if (nome) Serial.printf("  %s0x%02X  ->  %s\n", prefixo, addr, nome);
      else      Serial.printf("  %s0x%02X  ->  Desconhecido\n", prefixo, addr);
      found++;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }

  return found;
}

void taskI2CScan(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (xSemaphoreTake(mutexI2C, pdMS_TO_TICKS(3000)) != pdTRUE) {
      Serial.println("  I2C ocupado — scan cancelado.");
      if (semI2CScanDone) xSemaphoreGive(semI2CScanDone);
      continue;
    }

    uint8_t total = 0;

    Serial.println("\n====== SCAN I2C ======");
    Serial.println("--- Barramento principal ---");

    // No barramento principal mostra tudo, inclusive o TCA
    total += _scanBus("");

    // Verifica se o TCA existe
    Wire.beginTransmission(TCA_ADDR);
    bool tcaOk = (Wire.endTransmission() == 0);

    if (tcaOk) {
      for (uint8_t ch = 0; ch < 8; ch++) {
        // Seleciona somente um canal por vez
        Wire.beginTransmission(TCA_ADDR);
        Wire.write(1 << ch);
        if (Wire.endTransmission() != 0) {
          continue;
        }

        vTaskDelay(pdMS_TO_TICKS(5));

        char prefixo[16];
        snprintf(prefixo, sizeof(prefixo), "TCA CH%d | ", ch);

        // Dentro do canal, ignora o proprio TCA (0x70)
        uint8_t achados = _scanBus(prefixo, TCA_ADDR);
        total += achados;
      }

      // Fecha todos os canais ao final
      Wire.beginTransmission(TCA_ADDR);
      Wire.write(0x00);
      Wire.endTransmission();
    } else {
      Serial.println("  TCA9548A nao encontrado — canais nao varridos.");
    }

    xSemaphoreGive(mutexI2C);

    if (total == 0) {
      Serial.println("  Nenhum dispositivo encontrado.");
    }

    Serial.printf("Total: %d dispositivo(s)\n", total);
    Serial.println("======================\n");

    if (semI2CScanDone) xSemaphoreGive(semI2CScanDone);
  }
}