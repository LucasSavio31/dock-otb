#pragma once
#include "shared.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static constexpr const char* LOGDB_NS = "otb-logdb";

static void _logdbInsertLocked(const StoredLogEntry& entry) {
  if (!gLogEntries) return;
  uint16_t idx = (uint16_t)((gLogHead + gLogCount) % LOGDB_CAPACITY);
  if (gLogCount >= LOGDB_CAPACITY) {
    idx = gLogHead;
    gLogHead = (uint16_t)((gLogHead + 1) % LOGDB_CAPACITY);
  } else {
    gLogCount++;
  }
  gLogEntries[idx] = entry;
}

static void _logdbLoadFromNvs() {
  if (!gLogEntries) return;
  memset(gLogEntries, 0, sizeof(StoredLogEntry) * LOGDB_CAPACITY);
  gLogHead         = 0;
  gLogCount        = 0;
  gLogEpochBaseSec = 0;
  gLogEpochValid   = false;
  gLogBootId++;
  if (gLogNextSeq == 0) gLogNextSeq = 1;
}

static void _logdbSaveToNvsLocked() {
  // Persistencia do log em NVS foi desativada para nao comprometer
  // a memoria usada por calibracao, CAPDAC, ativacao e contadores.
}

static uint32_t _logdbCurrentEpochSecLocked() {
  if (!gLogEpochValid) return 0;
  const int64_t now = gLogEpochBaseSec + (int64_t)(millis() / 1000UL);
  return now > 0 ? (uint32_t)now : 0;
}

void logdbForceFlush() {
  if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(200)) == pdTRUE) {
    _logdbSaveToNvsLocked();
    xSemaphoreGive(mutexLogDb);
  }
}

void logdbSetEpoch(uint32_t epochSec) {
  if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(200)) == pdTRUE) {
    gLogEpochBaseSec = (int64_t)epochSec - (int64_t)(millis() / 1000UL);
    gLogEpochValid   = true;
    _logdbSaveToNvsLocked();
    xSemaphoreGive(mutexLogDb);
  }
}

void logdbClear() {
  if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(300)) == pdTRUE) {
    if (gLogEntries) memset(gLogEntries, 0, sizeof(StoredLogEntry) * LOGDB_CAPACITY);
    gLogHead    = 0;
    gLogCount   = 0;
    gLogNextSeq = 1;
    _logdbSaveToNvsLocked();
    xSemaphoreGive(mutexLogDb);
  }
}

void logdbPublish(const char* origin, const char* type, LogSeverity severity, const char* description) {
  if (!qLogEvent) return;
  LogEvent ev{};
  strncpy(ev.origin, origin ? origin : "Sistema", sizeof(ev.origin) - 1);
  strncpy(ev.type, type ? type : "Evento", sizeof(ev.type) - 1);
  strncpy(ev.description, description ? description : "", sizeof(ev.description) - 1);
  ev.severity = severity;
  (void)xQueueSend(qLogEvent, &ev, 0);
}

void logdbPublishf(const char* origin, const char* type, LogSeverity severity, const char* fmt, ...) {
  char msg[64] = {0};
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  logdbPublish(origin, type, severity, msg);
}

void taskLogDb(void *param) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(500)) == pdTRUE) {
    _logdbLoadFromNvs();
    _logdbSaveToNvsLocked();
    xSemaphoreGive(mutexLogDb);
  }

  Serial.printf("[LogDb] Banco iniciado. Entradas=%u boot=%lu\n",
                (unsigned)gLogCount, (unsigned long)gLogBootId);
  logdbPublishf("LogDb", "Inicializacao", LOG_INFO, "Banco de logs pronto no boot %lu.", (unsigned long)gLogBootId);

  uint8_t dirtyCount = 0;
  uint32_t lastFlush = millis();

  for (;;) {
    LogEvent ev{};
    if (xQueueReceive(qLogEvent, &ev, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (xSemaphoreTake(mutexLogDb, pdMS_TO_TICKS(200)) == pdTRUE) {
        StoredLogEntry entry{};
        entry.seq       = gLogNextSeq++;
        entry.bootId    = gLogBootId;
        entry.uptimeSec = millis() / 1000UL;
        entry.epochSec  = _logdbCurrentEpochSecLocked();
        entry.severity  = (uint8_t)ev.severity;
        strncpy(entry.origin, ev.origin, sizeof(entry.origin) - 1);
        strncpy(entry.type, ev.type, sizeof(entry.type) - 1);
        strncpy(entry.description, ev.description, sizeof(entry.description) - 1);
        _logdbInsertLocked(entry);
        dirtyCount++;
        xSemaphoreGive(mutexLogDb);
      }
    }

    if (dirtyCount >= 5 || (dirtyCount > 0 && (millis() - lastFlush) >= 10000UL)) {
      logdbForceFlush();
      dirtyCount = 0;
      lastFlush = millis();
    }
  }
}
