#pragma once
// =============================================================
//  task_erros.h — V5
//  Controle de codigos de erro OTB DockStation
//  Nextion e atualizado pela taskNextion via _enviarErros()
// =============================================================
#include "shared.h"

// =========================
// TABELA DE ERROS
// =========================
ErroOTB gErros[ERR_COUNT] = {
  {  0, "",  "Sistema OK",                false },
  {  1, "E", "Falha init FreeRTOS",       false },
  {  2, "E", "Falha NVS",                 false },
  {  3, "E", "Heap critico",              false },
  {  4, "E", "Reboot por WDT",            false },
  {101, "E", "NFC Caneta1 D13",           false },
  {102, "E", "NFC Caneta2 D14",           false },
  {103, "E", "NFC Caneta3 D4",            false },
  {104, "E", "NFC Cartucho1 D5",          false },
  {105, "E", "NFC Cartucho2 D15",         false },
  {106, "E", "NFC Cartucho3 D2",          false },
  {110, "E", "Falha leitura tag",         false },
  {111, "E", "Falha gravacao tag",        false },
  {112, "E", "Tag removida durante op",   false },
  {201, "E", "TCA9548A ausente",          false },
  {211, "E", "Nivel Cart1 CH0",           false },
  {212, "E", "Nivel Cart2 CH1",           false },
  {213, "E", "Nivel Cart3 CH2",           false },
  {220, "E", "FDC timeout leitura",       false },
  {221, "E", "FDC saturado RAW max",      false },
  {222, "E", "I2C recovery executado",    false },
  {301, "E", "Timeout recarga >60s",      false },
  {302, "E", "Bomba sem valvula aberta",  false },
  {311, "E", "Valvula 1 aberta >5min",    false },
  {312, "E", "Valvula 2 aberta >5min",    false },
  {313, "E", "Valvula 3 aberta >5min",    false },
  {401, "E", "Nextion sem resposta",      false },
  {402, "E", "Nextion nav falhou",        false },
};

static const char* ERRO_DESCRICAO_LONGA[ERR_COUNT] = {
  "Sistema operando normalmente",
  "Falha na inicializacao do FreeRTOS",
  "Falha ao acessar memoria NVS",
  "Heap livre critico - abaixo de 50KB",
  "Sistema reiniciado pelo watchdog WDT",
  "Erro NFC Caneta 1 (CS=D13)",
  "Erro NFC Caneta 2 (CS=D14)",
  "Erro NFC Caneta 3 (CS=D4)",
  "Erro NFC Cartucho 1 (CS=D5)",
  "Erro NFC Cartucho 2 (CS=D15)",
  "Erro NFC Cartucho 3 (CS=D2)",
  "Falha na leitura de dados da tag NFC",
  "Falha na gravacao de dados na tag NFC",
  "Tag removida durante operacao ativa",
  "MUX I2C TCA9548A nao encontrado (0x70)",
  "Erro Nivel de Cartucho 1 (CH0)",
  "Erro Nivel de Cartucho 2 (CH1)",
  "Erro Nivel de Cartucho 3 (CH2)",
  "FDC1004 timeout - medicao nao concluida",
  "FDC1004 saturado - eletrodo fora do range",
  "Barramento I2C travado - recovery aplicado",
  "Timeout de recarga - nivel nao atingido em 60s",
  "Bomba acionada sem valvula aberta",
  "Valvula 1 permanece aberta por mais de 5min",
  "Valvula 2 permanece aberta por mais de 5min",
  "Valvula 3 permanece aberta por mais de 5min",
  "Display Nextion nao responde na Serial2",
  "Navegacao de pagina no Nextion falhou",
};

// =========================
// CONTROLE
// =========================
void erroSetar(uint8_t idx) {
  if (idx == 0 || idx >= ERR_COUNT) return;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (!gErros[idx].ativo) {
      gErros[idx].ativo = true;
      logdbPublishf("Erros", "Falha", LOG_ERROR, "E%03u %s",
                    (unsigned)gErros[idx].codigo, gErros[idx].descricao);
    }
    xSemaphoreGive(mutexErros);
  }
}

void erroClear(uint8_t idx) {
  if (idx == 0 || idx >= ERR_COUNT) return;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(20)) == pdTRUE) {
    const bool wasActive = gErros[idx].ativo;
    gErros[idx].ativo = false;
    if (wasActive) {
      logdbPublishf("Erros", "Recuperacao", LOG_SUCCESS, "E%03u normalizado",
                    (unsigned)gErros[idx].codigo);
    }
    xSemaphoreGive(mutexErros);
  }
}

bool erroAtivo(uint8_t idx) {
  if (idx >= ERR_COUNT) return false;
  bool r = false;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(10)) == pdTRUE) {
    r = gErros[idx].ativo;
    xSemaphoreGive(mutexErros);
  }
  return r;
}

uint8_t erroGetPrimeiro() {
  uint8_t primeiro = 0;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (uint8_t i = 1; i < ERR_COUNT; i++) {
      if (gErros[i].ativo) { primeiro = i; break; }
    }
    xSemaphoreGive(mutexErros);
  }
  return primeiro;
}

uint16_t erroGetCodigo(uint8_t idx) {
  if (idx == 0 || idx >= ERR_COUNT) return 0;
  uint16_t c = 0;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(10)) == pdTRUE) {
    c = gErros[idx].codigo;
    xSemaphoreGive(mutexErros);
  }
  return c;
}

// Coleta ate 5 erros ativos — usado pela taskNextion
uint8_t erroGetAtivos(uint16_t* codigos, const char** descs, uint8_t maxCount) {
  uint8_t count = 0;
  if (xSemaphoreTake(mutexErros, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (uint8_t i = 1; i < ERR_COUNT && count < maxCount; i++) {
      if (gErros[i].ativo) {
        codigos[count] = gErros[i].codigo;
        descs[count]   = ERRO_DESCRICAO_LONGA[i];
        count++;
      }
    }
    xSemaphoreGive(mutexErros);
  }
  return count;
}

// =========================
// TASK ERROS
// Core 1, prioridade 1
// =========================
void taskErros(void *param) {
  vTaskDelay(pdMS_TO_TICKS(4000));
  Serial.println("[ERROS] Monitor iniciado.");

  for (;;) {
    if (esp_get_free_heap_size() < 50000) erroSetar(ERR_E003);
    else                                   erroClear(ERR_E003);

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
