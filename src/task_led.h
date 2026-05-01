#pragma once
#include "shared.h"

// =========================
// TASK LED
// Roda no Core 1, prioridade 1 (menor que as demais)
// Recebe xTaskNotify: 1 = tag presente, 0 = tag removida
// Padrões:
//   tag presente  -> LED fixo HIGH (feito pela taskNFC diretamente)
//   tag removida  -> LED OFF
//   cmd executado -> pisca 2x rápido (futuro: notificação com valor 2)
// =========================
void taskLED(void *param) {
  pinMode(LED2, OUTPUT);
  digitalWrite(LED2, LOW);

  uint32_t valor = 0;

  for (;;) {
    // Bloqueia aguardando notificação (máx 500ms)
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &valor, pdMS_TO_TICKS(500)) == pdTRUE) {
      switch (valor) {
        case 0:
          // Tag removida
          digitalWrite(LED2, LOW);
          break;
        case 1:
          // Tag presente — LED já ligado pela taskNFC, nada a fazer
          break;
        case 2:
          // Feedback de operação concluída: 2 piscadas rápidas
          for (int i = 0; i < 2; i++) {
            digitalWrite(LED2, LOW);
            vTaskDelay(pdMS_TO_TICKS(80));
            digitalWrite(LED2, HIGH);
            vTaskDelay(pdMS_TO_TICKS(80));
          }
          break;
        default:
          break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(9));
  }
}