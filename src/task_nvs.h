#pragma once
// =============================================================
//  task_nvs.h — V5
//  Persistência NVS: uptime acumulado + dados de ativação + recargas
//  - Ao boot: imprime NVS_ACTIVATION, NVS_USAGE e NVS_RECHARGE_COUNT no serial
//    (dashboard lê automaticamente ao conectar)
//  - A cada 60 s: atualiza usage_ms na NVS
// =============================================================
#include "shared.h"
#include <Preferences.h>

void taskNVS(void *param) {
  vTaskDelay(pdMS_TO_TICKS(3000)); // aguarda serial estabilizar

  // ── Carrega base de uptime de boots anteriores ────────────
  uint64_t uptimeBaseMs = 0;
  {
    if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
      Preferences p;
      p.begin("otb-dock", true); // read-only
      uptimeBaseMs = p.getULong64("usage_ms", 0);
      p.end();
      xSemaphoreGive(mutexNVS);
    }
  }

  // ── Imprime dados salvos (dashboard parseia ao conectar) ──
  {
    if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
      Preferences p;
      p.begin("otb-dock", true);

    // Versão do firmware
    Serial.println("FW_VERSION:" FIRMWARE_VERSION);

    // Ativação
    String activ = p.getString("activ", "null");
    Serial.print("NVS_ACTIVATION:");
    Serial.println(activ);

    // Uptime acumulado até o boot anterior + tempo atual
    uint64_t totalMs = uptimeBaseMs + (uint64_t)millis();
    Serial.printf("NVS_USAGE:%llu\n", totalMs);

    // Total acumulado de recargas/ciclos concluídos
    uint32_t rechargeCount = p.getUInt("rechg_cnt", 0);
    Serial.printf("NVS_RECHARGE_COUNT:%u\n", rechargeCount);

      p.end();
      xSemaphoreGive(mutexNVS);
    }
  }

  // ── Carrega calibracoes de sensor da NVS ─────────────────────
  {
    if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
      Preferences pc;
      pc.begin("otb-dock", true);
      for (uint8_t ch = 0; ch < 3; ch++) {
      char kv[8], kc[8], ks[8], kok[8];
      snprintf(kv,  sizeof(kv),  "cal_v%d",  ch);
      snprintf(kc,  sizeof(kc),  "cal_c%d",  ch);
      snprintf(ks,  sizeof(ks),  "cal_s%d",  ch);
      snprintf(kok, sizeof(kok), "cal_ok%d", ch);
      if (pc.isKey(kok) && pc.getUChar(kok, 0) == 1) {
        if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(200)) == pdTRUE) {
          gCalib[ch].vazioPf = pc.getFloat(kv, 0.0f);
          gCalib[ch].cheioPf = pc.getFloat(kc, 1.0f);
          gCalib[ch].step    = (uint8_t)pc.getUInt(ks, 10);
          gCalib[ch].valid   = true;
          xSemaphoreGive(mutexCalib);
        }
        Serial.printf("[NVS] Calib CH%d: vazio=%.4f cheio=%.4f step=%u\n",
                      ch, gCalib[ch].vazioPf, gCalib[ch].cheioPf, gCalib[ch].step);
      }
      // Carrega CAPDAC
      char kde[9], kdp[9];
      snprintf(kde, sizeof(kde), "cdac_e%d", ch);
      snprintf(kdp, sizeof(kdp), "cdac_p%d", ch);
      if (pc.isKey(kde)) {
        bool  en  = pc.getUChar(kde, 0) != 0;
        float pfv = pc.getFloat(kdp, 0.0f);
        if (xSemaphoreTake(mutexCalib, pdMS_TO_TICKS(200)) == pdTRUE) {
          gCalib[ch].capdacEn = en;
          gCalib[ch].capdacPf = pfv;
          xSemaphoreGive(mutexCalib);
        }
        // taskSensor pode ter inicializado o chip antes desta leitura (race boot)
        // dirty flag garante que o chip sera re-inicializado com CAPDAC correto
        if (en) {
          gCalibDirty[ch] = true;
          Serial.printf("[NVS] CAPDAC CH%d: %.4f pF\n", ch, pfv);
        }
      }
      }
      pc.end();
      xSemaphoreGive(mutexNVS);
    }
  }

  Serial.printf("[NVS] Base uptime: %llu ms\n", uptimeBaseMs);

  uint8_t tick = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Salva uptime a cada 60 s (12 × 5 s)
    if (++tick >= 12) {
      tick = 0;
      uint64_t totalMs = uptimeBaseMs + (uint64_t)millis();
      if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(300)) == pdTRUE) {
        Preferences p;
        p.begin("otb-dock", false);
        p.putULong64("usage_ms", totalMs);
        p.end();
        xSemaphoreGive(mutexNVS);
      }
    }
  }
}
