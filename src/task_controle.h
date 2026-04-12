#pragma once
// =============================================================
//  task_controle.h — V5
//  Bomba PWM D25 | Válvulas D26/D27/D33
//  Recebe comandos via qControleCmd
//  Roda no Core 1, prioridade 2
// =============================================================
#include "shared.h"

static bool    _bombaLigada  = false;
static uint8_t _dutyAtual    = BOMBA_PWM_DUTY;
static bool    _valvulas[4]  = {false, false, false, false};

static void _bombaAplicar(uint8_t duty) {
  _dutyAtual = duty;
  ledcWrite(BOMBA_PWM_CANAL, _bombaLigada ? duty : 0);
}

static void _bombaLigar() {
  if (!_bombaLigada) {
    _bombaLigada = true;
    ledcWrite(BOMBA_PWM_CANAL, _dutyAtual);
    Serial.printf("[CTRL] Bomba LIGADA duty=%d/255 (%.0f%%)\n",
                  _dutyAtual, (_dutyAtual / 255.0f) * 100.0f);
  }
}

static void _bombaDesligar() {
  if (_bombaLigada) {
    _bombaLigada = false;
    ledcWrite(BOMBA_PWM_CANAL, 0);
    Serial.println("[CTRL] Bomba DESLIGADA");
  }
}

static void _valvulaSet(uint8_t idx, bool on) {
  if (idx < 1 || idx > 3) return;
  _valvulas[idx] = on;
  const uint8_t pins[4] = {0, VALVULA_1, VALVULA_2, VALVULA_3};
  digitalWrite(pins[idx], on ? HIGH : LOW);
  Serial.printf("[CTRL] Valvula %d %s\n", idx, on ? "LIGADA" : "DESLIGADA");
}

void taskControle(void *param) {
  ledcSetup(BOMBA_PWM_CANAL, BOMBA_PWM_FREQ, BOMBA_PWM_RES);
  ledcAttachPin(BOMBA_PIN, BOMBA_PWM_CANAL);
  ledcWrite(BOMBA_PWM_CANAL, 0);

  pinMode(VALVULA_1, OUTPUT); digitalWrite(VALVULA_1, LOW);
  pinMode(VALVULA_2, OUTPUT); digitalWrite(VALVULA_2, LOW);
  pinMode(VALVULA_3, OUTPUT); digitalWrite(VALVULA_3, LOW);

  Serial.println("[CTRL] Pronto. Bomba=D25 | V1=D26 V2=D27 V3=D33");

  ControleCmd cmd;
  for (;;) {
    if (xQueueReceive(qControleCmd, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
      switch (cmd.type) {
        case ControleCmd::BOMBA_ON:   _bombaLigar();               break;
        case ControleCmd::BOMBA_OFF:  _bombaDesligar();            break;
        case ControleCmd::BOMBA_DUTY: {
          uint8_t pct  = constrain(cmd.payload, 0, 100);
          uint8_t duty = (uint8_t)((pct / 100.0f) * 255.0f);
          _bombaAplicar(duty);
          if (pct > 0 && !_bombaLigada) _bombaLigar();
          if (pct == 0 && _bombaLigada) _bombaDesligar();
          Serial.printf("[CTRL] Bomba duty=%d%% (%d/255)\n", pct, duty);
          break;
        }
        case ControleCmd::VALVULA_ON:  _valvulaSet(cmd.payload, true);  break;
        case ControleCmd::VALVULA_OFF: _valvulaSet(cmd.payload, false); break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}