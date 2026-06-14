#pragma once
// =============================================================
//  task_atuadores.h — V5
//  Controla bomba (PWM LEDC) e válvulas (GPIO)
//  Recebe de duas filas:
//    qControleCmd — ControleCmd (taskSerial: ON/OFF/DUTY explícito)
//    qActCmd      — ActCmd      (taskNextion: botões da página configs)
//  Core 0, prioridade 2
// =============================================================
#include "shared.h"
#include "task_erros.h"
#include "driver/ledc.h"

// =========================
// CONFIGURAÇÃO PWM DA BOMBA
// =========================
#define BOMBA_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BOMBA_LEDC_TIMER    LEDC_TIMER_0
#define BOMBA_LEDC_FREQ     1000              // Hz
#define BOMBA_LEDC_RES      LEDC_TIMER_8_BIT // 0–255

static void _bombaSetDuty(uint8_t duty) {
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, BOMBA_LEDC_CHANNEL, duty);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, BOMBA_LEDC_CHANNEL);
}

static void _valvula(uint8_t num, bool state) {
  gpio_num_t pin;
  switch (num) {
    case 1: pin = (gpio_num_t)VALVULA_1; break;
    case 2: pin = (gpio_num_t)VALVULA_2; break;
    case 3: pin = (gpio_num_t)VALVULA_3; break;
    default: return;
  }
  gpio_set_level(pin, state ? 1 : 0);
  Serial.printf("[Atuadores] VALVULA %d -> %s\n", num, state ? "ON" : "OFF");
}

// =========================
// TASK ATUADORES
// =========================
void taskAtuadores(void *param) {

  // --- PWM bomba ---
  ledc_timer_config_t timer = {
    .speed_mode      = LEDC_HIGH_SPEED_MODE,
    .duty_resolution = BOMBA_LEDC_RES,
    .timer_num       = BOMBA_LEDC_TIMER,
    .freq_hz         = BOMBA_LEDC_FREQ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer);

  ledc_channel_config_t channel = {
    .gpio_num   = BOMBA_PIN,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel    = BOMBA_LEDC_CHANNEL,
    .timer_sel  = BOMBA_LEDC_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ledc_channel_config(&channel);

  // --- GPIO válvulas ---
  gpio_set_direction((gpio_num_t)VALVULA_1, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)VALVULA_2, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)VALVULA_3, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)VALVULA_1, 0);
  gpio_set_level((gpio_num_t)VALVULA_2, 0);
  gpio_set_level((gpio_num_t)VALVULA_3, 0);

  // Estado das válvulas (índices 1-3 usados)
  bool stVal[4]  = {false, false, false, false};

  // Purga temporizada — 0 = inativa; usa inicio+elapsed para evitar rollover de millis()
  uint32_t purgeStartMs = 0;

  Serial.println("[Atuadores] Pronto.");

  for (;;) {
    // ── Bloqueio por violação: atuadores desligados, filas drenadas ──
    if (gBloqueado) {
      _bombaSetDuty(0);
      gpio_set_level((gpio_num_t)VALVULA_1, 0);
      gpio_set_level((gpio_num_t)VALVULA_2, 0);
      gpio_set_level((gpio_num_t)VALVULA_3, 0);
      gBombaDuty  = 0;
      ControleCmd cc; while (xQueueReceive(qControleCmd, &cc, 0) == pdTRUE) {}
      ActCmd      ac; while (xQueueReceive(qActCmd,      &ac, 0) == pdTRUE) {}
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    uint32_t now = millis();

    // ── Fim de purga ────────────────────────────────────────
    if (purgeStartMs > 0 && (now - purgeStartMs >= PURGE_DURATION_MS)) {
      _bombaSetDuty(0);
      purgeStartMs = 0;
      Serial.println("[Atuadores] BOMBA -> FIM PURGA");
    }

    // ── qControleCmd: comandos explícitos do menu serial ────
    ControleCmd cc;
    while (xQueueReceive(qControleCmd, &cc, 0) == pdTRUE) {
      switch (cc.type) {
        case ControleCmd::VALVULA_ON:
          stVal[cc.payload] = true;
          _valvula(cc.payload, true);
          break;
        case ControleCmd::VALVULA_OFF:
          stVal[cc.payload] = false;
          _valvula(cc.payload, false);
          break;
        case ControleCmd::BOMBA_ON:
          purgeStartMs = 0;  // cancela purga se ativa
          if (!stVal[1] && !stVal[2] && !stVal[3]) erroSetar(ERR_E302);
          else                                      erroClear(ERR_E302);
          _bombaSetDuty(255);
          Serial.println("[Atuadores] BOMBA -> ON (100%)");
          break;
        case ControleCmd::BOMBA_OFF:
          purgeStartMs = 0;
          erroClear(ERR_E302);
          _bombaSetDuty(0);
          Serial.println("[Atuadores] BOMBA -> OFF");
          break;
        case ControleCmd::BOMBA_DUTY: {
          purgeStartMs = 0;
          if (cc.payload > 0 && !stVal[1] && !stVal[2] && !stVal[3]) erroSetar(ERR_E302);
          else                                                          erroClear(ERR_E302);
          uint8_t duty = (uint8_t)((cc.payload / 100.0f) * 255.0f);
          _bombaSetDuty(duty);
          gBombaDuty = cc.payload;
          Serial.printf("[Atuadores] BOMBA duty=%d%%\n", cc.payload);
          break;
        }
      }
    }

    // ── qActCmd: botões da página configs do Nextion ────────
    ActCmd ac;
    while (xQueueReceive(qActCmd, &ac, 0) == pdTRUE) {
      switch (ac.type) {

        case ActCmd::ACT_PURGAR:
          if (purgeStartMs == 0) {
            // Inicia purga temporizada
            if (!stVal[1] && !stVal[2] && !stVal[3]) erroSetar(ERR_E302);
            else                                      erroClear(ERR_E302);
            _bombaSetDuty(255);
            purgeStartMs = millis();
            Serial.printf("[Atuadores] BOMBA -> PURGA %dms\n", PURGE_DURATION_MS);
          } else {
            // Cancela purga em andamento
            erroClear(ERR_E302);
            _bombaSetDuty(0);
            purgeStartMs = 0;
            Serial.println("[Atuadores] BOMBA -> PURGA CANCELADA");
          }
          break;

        case ActCmd::ACT_VALVULA_1:
          stVal[1] = !stVal[1];
          _valvula(1, stVal[1]);
          break;

        case ActCmd::ACT_VALVULA_2:
          stVal[2] = !stVal[2];
          _valvula(2, stVal[2]);
          break;

        case ActCmd::ACT_VALVULA_3:
          stVal[3] = !stVal[3];
          _valvula(3, stVal[3]);
          break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}