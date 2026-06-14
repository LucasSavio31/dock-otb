#pragma once
// =============================================================
//  task_violacao.h
//  Detecção de violação física via D34 (input-only, pull-up ext.)
//  Fluxo:
//    Boot → lê NVS "locked" → se true seta gBloqueado imediatamente
//    Runtime → monitora D34; LOW por ≥50ms = violação
//    Violação → grava NVS "locked"=1 → seta gBloqueado = true
//    Desbloqueio → via comando serial "unlock 1234" (task_serial.h)
//                  que limpa NVS "locked" e chama esp_restart()
// =============================================================
#include "shared.h"
#include <Preferences.h>

void taskViolacao(void *param) {
  // D34 é input-only; sem pull-up interno — requer resistor externo 10kΩ → 3.3V
  pinMode(VIOLACAO_PIN, INPUT);

  // Aguarda mutexNVS estar disponível (criado antes das tasks)
  vTaskDelay(pdMS_TO_TICKS(500));

  // Verifica bloqueio persistente da última sessão
  if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(600)) == pdTRUE) {
    Preferences p;
    p.begin("otb-dock", true);
    bool lockedNvs = p.getUChar("locked", 0) != 0;
    p.end();
    xSemaphoreGive(mutexNVS);
    if (lockedNvs) {
      gBloqueado = true;
      Serial.println("[Violacao] Bloqueio persistente detectado — equipamento BLOQUEADO.");
      Serial.println("DOCK_BLOCKED");
    }
  }

  // Janela de imunidade pós-boot: aguarda pino estabilizar em HIGH antes de monitorar.
  // Evita re-bloqueio imediato se o botão ainda estiver pressionado ao reiniciar.
  {
    uint32_t t0 = millis();
    while (digitalRead(VIOLACAO_PIN) == LOW && (millis() - t0 < 10000)) {
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }

  for (;;) {
    if (!gBloqueado) {
      if (digitalRead(VIOLACAO_PIN) == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50)); // debounce
        if (digitalRead(VIOLACAO_PIN) == LOW) {
          Serial.println("[Violacao] VIOLACAO DETECTADA! Gravando bloqueio na NVS...");

          if (mutexNVS && xSemaphoreTake(mutexNVS, pdMS_TO_TICKS(500)) == pdTRUE) {
            Preferences p;
            p.begin("otb-dock", false);
            p.putUChar("locked", 1);
            p.end();
            xSemaphoreGive(mutexNVS);
          }

          gBloqueado = true;
          Serial.println("DOCK_BLOCKED");

          // Aguarda soltar o botão antes de continuar
          while (digitalRead(VIOLACAO_PIN) == LOW) {
            vTaskDelay(pdMS_TO_TICKS(100));
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
