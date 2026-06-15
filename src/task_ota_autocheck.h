#pragma once
// =============================================================
//  task_ota_autocheck.h
//  Verificação periódica e autônoma de novas releases no GitHub.
//  Core 1 | Prio 1 | Stack 10240 (WiFiClientSecure + HTTPClient + JSON)
//
//  Fluxo:
//    Boot +30s → verifica GitHub (WiFi e DNS já estabilizados)
//    Sucesso   → repete a cada 1h
//    Falha     → retry em 60s (máx 5 tentativas, depois 1h)
//    Skip      → se taskOTA estiver ocupado (update/download em andamento)
// =============================================================
#include "shared.h"
#include "task_ota.h"   // acessa _otaEnsureWifi, _otaCheckGithub, mutexOta, gOtaStatus

void taskOtaAutoCheck(void *param) {
  vTaskDelay(pdMS_TO_TICKS(30000)); // aguarda sistema e WiFi estabilizarem

  uint8_t failCount = 0;

  for (;;) {
    // Não verifica enquanto taskOTA está executando update, download ou validação
    bool busy = false;
    if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(200)) == pdTRUE) {
      OtaStateEnum st = gOtaStatus.state;
      busy = (st == OTA_STATE_DOWNLOADING   ||
              st == OTA_STATE_FLASHING       ||
              st == OTA_STATE_WIFI_CONNECTING||
              st == OTA_STATE_VALIDATING);
      xSemaphoreGive(mutexOta);
    }

    if (busy) {
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }

    // Garante WiFi conectado — reconecta se necessário
    if (!_otaEnsureWifi()) {
      vTaskDelay(pdMS_TO_TICKS(30000));
      continue;
    }

    char latestVer[16]    = {0};
    char downloadUrl[256] = {0};
    Serial.println("OTA_INFO:Verificando releases no GitHub...");

    bool found = _otaCheckGithub(latestVer, sizeof(latestVer),
                                 downloadUrl, sizeof(downloadUrl));

    if (found) {
      bool available = (strcasecmp(latestVer, FIRMWARE_VERSION) != 0);
      if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(200)) == pdTRUE) {
        strncpy(gOtaStatus.latestVersion, latestVer,
                sizeof(gOtaStatus.latestVersion) - 1);
        gOtaStatus.updateAvailable = available;
        if (gOtaStatus.state == OTA_STATE_DONE_ERR ||
            gOtaStatus.state == OTA_STATE_CHECKING) {
          gOtaStatus.state = OTA_STATE_IDLE;
        }
        xSemaphoreGive(mutexOta);
      }
      Serial.printf("OTA_CHECK_OK:fw=%s,latest=%s,available=%d\n",
                    FIRMWARE_VERSION, latestVer, available ? 1 : 0);
      failCount = 0;
      vTaskDelay(pdMS_TO_TICKS(3600000UL)); // próxima verificação em 1h
    } else {
      if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (gOtaStatus.state == OTA_STATE_DONE_ERR ||
            gOtaStatus.state == OTA_STATE_CHECKING) {
          gOtaStatus.state = OTA_STATE_IDLE;
        }
        xSemaphoreGive(mutexOta);
      }
      failCount++;
      uint32_t retryMs = (failCount >= 5) ? 3600000UL : 60000UL;
      vTaskDelay(pdMS_TO_TICKS(retryMs));
    }
  }
}
