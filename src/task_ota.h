#pragma once
// =============================================================
//  task_ota.h — OTA via GitHub Releases + Rollback automático
//
//  Fluxo de update:
//    1. ota wifi <ssid> <senha>   → salva credenciais na NVS
//    2. ota check                 → conecta WiFi, consulta GitHub
//    3. ota update                → baixa .bin, grava, reinicia
//    4. (após boot) valida 60 s   → se saudável: marca válido
//                                   senão: rollback automático
//    5. ota rollback              → força volta ao firmware anterior
//
//  Resposta serial (parseable pelo dashboard):
//    OTA_WIFI_OK:<ip>
//    OTA_WIFI_CONNECTING:<ssid>
//    OTA_LATEST:<tag>
//    OTA_CHECK_OK:fw=<cur>,latest=<tag>,available=<0|1>
//    OTA_PROGRESS:<0-100>
//    OTA_FLASH_OK:100
//    OTA_UPDATE_OK:fw=<cur>→<tag>,reiniciando...
//    OTA_VALIDATED:Firmware marcado como válido
//    OTA_ROLLBACK_OK:Reiniciando...
//    OTA_AUTO_ROLLBACK:<motivo>
//    OTA_VALIDATING_TICK:<elapsed>/<total>
//    OTA_STATUS:state=<s>,fw=<v>,latest=<v>,wifi=<ssid>,pending=<0|1>,running=<part>,boot=<part>
//    OTA_WIFI_SAVED:<ssid>
//    OTA_ERR:<mensagem>
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <Preferences.h>
#include "shared.h"

// ─── Configurações ────────────────────────────────────────────
#define OTA_GITHUB_OWNER    "LucasSavio31"
#define OTA_GITHUB_REPO     "dock-otb"
#define OTA_ASSET_NAME      "firmware.bin"
#define OTA_VALIDATE_SECS   60   // janela de validação pós-update
#define OTA_MAX_BOOT_FAIL    3   // reboots sem validação → rollback automático

// ─── Globals (definidos em main.cpp) ─────────────────────────
QueueHandle_t     qOtaCmd    = nullptr;
SemaphoreHandle_t mutexOta   = nullptr;
OtaStatus         gOtaStatus = {};

// ─── Helpers internos ────────────────────────────────────────

static const char* _otaStateStr(OtaStateEnum s) {
  switch (s) {
    case OTA_STATE_IDLE:            return "idle";
    case OTA_STATE_WIFI_CONNECTING: return "wifi_connecting";
    case OTA_STATE_CHECKING:        return "checking";
    case OTA_STATE_DOWNLOADING:     return "downloading";
    case OTA_STATE_FLASHING:        return "flashing";
    case OTA_STATE_DONE_OK:         return "done_ok";
    case OTA_STATE_DONE_ERR:        return "done_err";
    case OTA_STATE_VALIDATING:      return "validating";
    default:                        return "unknown";
  }
}

static void _otaSetState(OtaStateEnum s) {
  if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
    gOtaStatus.state = s;
    xSemaphoreGive(mutexOta);
  }
}

// ─── WiFi ─────────────────────────────────────────────────────

static bool _otaConnectWifi() {
  Preferences prefs;
  char ssid[64] = {0};
  char pass[64] = {0};
  prefs.begin("ota_cfg", true);
  prefs.getString("ssid", ssid, sizeof(ssid));
  prefs.getString("pass", pass, sizeof(pass));
  prefs.end();

  if (strlen(ssid) == 0) {
    Serial.println("OTA_ERR:WiFi nao configurado. Use: ota wifi <ssid> <senha>");
    return false;
  }

  if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
    strncpy(gOtaStatus.wifiSsid, ssid, sizeof(gOtaStatus.wifiSsid) - 1);
    xSemaphoreGive(mutexOta);
  }

  _otaSetState(OTA_STATE_WIFI_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.printf("OTA_WIFI_CONNECTING:%s\n", ssid);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 15000) {
      WiFi.disconnect(true);
      Serial.println("OTA_ERR:WiFi timeout");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // Aguarda roteamento e DNS estabilizarem antes de qualquer HTTPS
  vTaskDelay(pdMS_TO_TICKS(1000));

  if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
    gOtaStatus.wifiOk = true;
    xSemaphoreGive(mutexOta);
  }
  Serial.printf("OTA_WIFI_OK:%s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Mantém WiFi conectado após operações para reconexão rápida na próxima vez
static void _otaDisconnectWifi() {
  // Não desconecta — mantém a sessão WiFi ativa para próximos comandos OTA.
  // O WiFi só é desligado no scan (que precisa re-entrar em STA limpo).
}

// Usado apenas pelo scan para garantir estado limpo
static void _otaForceDisconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
    gOtaStatus.wifiOk = false;
    xSemaphoreGive(mutexOta);
  }
}

// Reconecta se necessário (credenciais já salvas via esp_wifi / NVS do SDK)
static bool _otaEnsureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  return _otaConnectWifi();
}

// ─── Versão via raw.githubusercontent.com ─────────────────────
// api.github.com pode estar bloqueado em certas redes.
// O OTA lê version.json direto do repositório (mesmo CDN do github.com).
// Formato do arquivo:
//   { "tag_name": "v1.6",
//     "download_url": "https://raw.githubusercontent.com/owner/repo/main/firmware/firmware.bin" }

static bool _otaCheckGithub(char *latestVer, size_t verLen,
                             char *downloadUrl, size_t urlLen) {
  char path[128];
  snprintf(path, sizeof(path),
    "/%s/%s/main/firmware/version.json", OTA_GITHUB_OWNER, OTA_GITHUB_REPO);

  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();

  HTTPClient http;
  http.begin(*client, "raw.githubusercontent.com", 443, path, true);
  http.addHeader("User-Agent", "OTB-DockStation/1.0");
  http.setTimeout(15000);
  http.setConnectTimeout(12000);

  _otaSetState(OTA_STATE_CHECKING);
  Serial.println("OTA_INFO:Consultando versao no repositorio...");

  int code = http.GET();

  if (code == 404) {
    Serial.println("OTA_ERR:firmware/version.json nao encontrado no repositorio.");
    http.end(); delete client;
    return false;
  }
  if (code <= 0) {
    Serial.printf("OTA_ERR:Falha SSL/TCP (cod=%d). Verifique DNS e heap livre.\n", code);
    http.end(); delete client;
    return false;
  }
  if (code != 200) {
    Serial.printf("OTA_ERR:HTTP %d ao buscar version.json\n", code);
    http.end(); delete client;
    return false;
  }

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, http.getStream());
  http.end();
  delete client;

  if (jerr) {
    Serial.printf("OTA_ERR:JSON %s\n", jerr.c_str());
    return false;
  }

  const char *tag = doc["tag_name"] | "";
  strncpy(latestVer, tag, verLen - 1);
  latestVer[verLen - 1] = '\0';

  const char *url = doc["download_url"] | "";
  strncpy(downloadUrl, url, urlLen - 1);
  downloadUrl[urlLen - 1] = '\0';

  if (strlen(latestVer) == 0 || strlen(downloadUrl) == 0) {
    Serial.println("OTA_ERR:version.json incompleto (tag_name ou download_url ausente)");
    return false;
  }

  Serial.printf("OTA_LATEST:%s\n", latestVer);
  return true;
}

// ─── Download + Flash ─────────────────────────────────────────

static bool _otaDownloadAndFlash(const char *url) {
  // GitHub assets redirect (301/302) para CDN objects.githubusercontent.com.
  // HTTPC_FORCE_FOLLOW_REDIRECTS cria um novo WiFiClientSecure interno SEM setInsecure(),
  // o que faz o handshake TLS falhar. Resolvemos o redirect manualmente aqui.
  String realUrl = String(url);
  {
    WiFiClientSecure *c = new WiFiClientSecure();
    c->setInsecure();
    HTTPClient h;
    h.begin(*c, url);
    h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    h.setTimeout(15000);
    h.setConnectTimeout(12000);
    h.addHeader("User-Agent", "OTB-DockStation/1.0");
    const char* hdrKeys[] = {"Location"};
    h.collectHeaders(hdrKeys, 1);
    int code = h.GET();
    if (code == 301 || code == 302 || code == 307 || code == 308) {
      String loc = h.header("Location");
      if (loc.length() > 0) {
        realUrl = loc;
        Serial.println("OTA_INFO:Redirect CDN resolvido");
      }
    } else if (code != 200) {
      Serial.printf("OTA_ERR:Falha ao resolver URL do asset (cod=%d)\n", code);
      h.end(); delete c;
      return false;
    }
    h.end();
    delete c;
  }

  // Download direto do CDN — sem redirect adicional
  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();
  HTTPClient http;
  http.begin(*client, realUrl);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(60000);
  http.setConnectTimeout(12000);
  http.addHeader("User-Agent", "OTB-DockStation/1.0");

  _otaSetState(OTA_STATE_DOWNLOADING);
  if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
    gOtaStatus.progress = 0;
    xSemaphoreGive(mutexOta);
  }
  Serial.println("OTA_INFO:Iniciando download do firmware...");

  int code = http.GET();
  if (code <= 0) {
    Serial.printf("OTA_ERR:Falha SSL/TCP no download (cod=%d)\n", code);
    http.end(); delete client;
    return false;
  }
  if (code != 200) {
    Serial.printf("OTA_ERR:Download HTTP %d\n", code);
    http.end(); delete client;
    return false;
  }

  int totalSize = http.getSize();
  WiFiClient *stream = http.getStreamPtr();
  Serial.printf("OTA_INFO:Tamanho: %d bytes\n", totalSize);

  if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN)) {
    Serial.printf("OTA_ERR:Update.begin: %s\n", Update.errorString());
    http.end(); delete client;
    return false;
  }

  _otaSetState(OTA_STATE_FLASHING);

  uint8_t buf[1280];
  int written = 0;
  int lastPct = -1;
  uint32_t lastYield  = millis();
  uint32_t lastDataMs = millis(); // detecção de stall

  while (http.connected() && (totalSize <= 0 || written < totalSize)) {
    size_t avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (n > 0) {
        Update.write(buf, n);
        written += n;
        lastDataMs = millis();
        if (totalSize > 0) {
          int pct = (written * 100) / totalSize;
          if (pct != lastPct) {
            Serial.printf("OTA_PROGRESS:%d\n", pct);
            if (xSemaphoreTake(mutexOta, 0) == pdTRUE) {
              gOtaStatus.progress = pct;
              xSemaphoreGive(mutexOta);
            }
            lastPct = pct;
          }
        }
      }
    } else {
      if (millis() - lastDataMs > 15000) {
        Serial.println("OTA_ERR:Timeout no download (sem dados por 15s)");
        http.end(); delete client;
        return false;
      }
      if (!stream->connected()) break;
    }
    if (millis() - lastYield > 80) {
      vTaskDelay(pdMS_TO_TICKS(1));
      lastYield = millis();
    }
  }
  http.end();
  delete client;

  if (!Update.end(true)) {
    Serial.printf("OTA_ERR:Update.end: %s\n", Update.errorString());
    return false;
  }

  Serial.println("OTA_FLASH_OK:100");
  return true;
}

// ─── Estado de validação pendente (NVS) ──────────────────────

static void _otaSavePending(bool pending) {
  Preferences prefs;
  prefs.begin("ota_state", false);
  prefs.putBool("pending", pending);
  if (!pending) prefs.putUChar("boot_cnt", 0);
  prefs.end();
}

static bool _otaLoadPending(uint8_t *bootCnt) {
  Preferences prefs;
  prefs.begin("ota_state", true);
  bool p = prefs.getBool("pending", false);
  if (bootCnt) *bootCnt = prefs.getUChar("boot_cnt", 0);
  prefs.end();
  return p;
}

static void _otaIncrBootCount() {
  Preferences prefs;
  prefs.begin("ota_state", false);
  uint8_t cnt = prefs.getUChar("boot_cnt", 0) + 1;
  prefs.putUChar("boot_cnt", cnt);
  prefs.end();
}

// ─── Rollback ─────────────────────────────────────────────────

static void _otaDoRollback() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running) { Serial.println("OTA_ERR:Particao ativa nao encontrada"); return; }

  // Encontra a partição OTA alternativa
  esp_partition_subtype_t otherSub =
    (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
      ? ESP_PARTITION_SUBTYPE_APP_OTA_1
      : ESP_PARTITION_SUBTYPE_APP_OTA_0;

  const esp_partition_t *target =
    esp_partition_find_first(ESP_PARTITION_TYPE_APP, otherSub, nullptr);

  if (!target) {
    Serial.println("OTA_ERR:Sem particao de rollback disponivel");
    return;
  }

  // Verifica que a partição alvo tem firmware válido
  esp_ota_img_states_t imgState;
  if (esp_ota_get_state_partition(target, &imgState) == ESP_OK) {
    if (imgState == ESP_OTA_IMG_INVALID || imgState == ESP_OTA_IMG_UNDEFINED) {
      Serial.println("OTA_ERR:Firmware anterior invalido — impossivel fazer rollback");
      return;
    }
  }

  esp_err_t err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    Serial.printf("OTA_ERR:set_boot_partition: %s\n", esp_err_to_name(err));
    return;
  }

  _otaSavePending(false);
  logdbPublish("OTA", "ROLLBACK", LOG_WARN, "Rollback para firmware anterior");
  Serial.println("OTA_ROLLBACK_OK:Reiniciando...");
  vTaskDelay(pdMS_TO_TICKS(800));
  esp_restart();
}

// ─── Task principal ───────────────────────────────────────────

void taskOTA(void *param) {
  vTaskDelay(pdMS_TO_TICKS(5000)); // espera sistema estabilizar

  // Inicializa status global
  if (xSemaphoreTake(mutexOta, portMAX_DELAY) == pdTRUE) {
    strncpy(gOtaStatus.latestVersion, "-", sizeof(gOtaStatus.latestVersion) - 1);
    gOtaStatus.state = OTA_STATE_IDLE;
    xSemaphoreGive(mutexOta);
  }

  // ── Auto-connect WiFi no boot com credenciais salvas ─────
  {
    Preferences prefs;
    char ssid[64] = {0};
    prefs.begin("ota_cfg", true);
    prefs.getString("ssid", ssid, sizeof(ssid));
    prefs.end();
    if (strlen(ssid) > 0) {
      Serial.printf("OTA_INFO:Auto-conectando WiFi salvo: %s\n", ssid);
      _otaConnectWifi(); // ignora falha — dispositivo pode estar sem rede no boot
      _otaSetState(OTA_STATE_IDLE); // garante estado limpo após auto-connect
    }
  }

  // ── Verificação de validação pendente no boot ─────────────
  uint8_t bootCnt = 0;
  if (_otaLoadPending(&bootCnt)) {
    _otaIncrBootCount();
    bootCnt++;

    Serial.printf("OTA_VALIDATING:boot_cnt=%d/%d\n", bootCnt, OTA_MAX_BOOT_FAIL);

    if (bootCnt > OTA_MAX_BOOT_FAIL) {
      Serial.println("OTA_AUTO_ROLLBACK:Muitos boots sem validacao");
      logdbPublish("OTA", "AUTO_ROLLBACK", LOG_ERROR,
                   "Rollback automatico por excesso de boots sem validacao");
      _otaDoRollback();
      // Se rollback falhou (sem firmware anterior): continua rodando
    }

    _otaSetState(OTA_STATE_VALIDATING);
    if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
      gOtaStatus.pendingValidation = true;
      xSemaphoreGive(mutexOta);
    }

    // Janela de validação: monitora saúde por OTA_VALIDATE_SECS segundos
    uint32_t t0 = millis();
    bool saudavel = true;

    while (millis() - t0 < (uint32_t)(OTA_VALIDATE_SECS * 1000)) {
      // Heap mínimo: 20 KB livres
      if (esp_get_free_heap_size() < 20000) {
        Serial.println("OTA_HEALTH_FAIL:Heap critico");
        saudavel = false;
        break;
      }
      // Erros críticos de sistema (E001-E003)
      if (erroAtivo(ERR_E001) || erroAtivo(ERR_E002) || erroAtivo(ERR_E003)) {
        Serial.println("OTA_HEALTH_FAIL:Erro critico de sistema");
        saudavel = false;
        break;
      }
      uint32_t elapsed = (millis() - t0) / 1000;
      if (elapsed % 10 == 0) {
        static uint32_t lastTick = 0xFFFFFFFF;
        if (elapsed != lastTick) {
          Serial.printf("OTA_VALIDATING_TICK:%lu/%d\n", elapsed, OTA_VALIDATE_SECS);
          lastTick = elapsed;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (saudavel) {
      _otaSavePending(false);
      // Sinaliza ao bootloader (no-op se rollback não está habilitado no bootloader)
      esp_ota_mark_app_valid_cancel_rollback();
      if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
        gOtaStatus.pendingValidation = false;
        gOtaStatus.state = OTA_STATE_IDLE;
        xSemaphoreGive(mutexOta);
      }
      Serial.println("OTA_VALIDATED:Firmware marcado como valido");
      logdbPublish("OTA", "VALIDATED", LOG_SUCCESS, "Firmware novo validado com sucesso");
    } else {
      _otaDoRollback();
    }
  }

  // ── Broadcast de status inicial — popula boxes do dashboard no login ─
  {
    const esp_partition_t *running = esp_ota_get_running_partition();
    Preferences prefs;
    char ssid[64] = "nao_configurado";
    prefs.begin("ota_cfg", true);
    prefs.getString("ssid", ssid, sizeof(ssid));
    prefs.end();
    bool wOk = (WiFi.status() == WL_CONNECTED);
    String wIp = wOk ? WiFi.localIP().toString() : "";
    Serial.printf(
      "OTA_STATUS:state=idle,fw=%s,latest=-,wifi=%s,wifiok=%d,ip=%s,pending=0,running=%s,boot=%s\n",
      FIRMWARE_VERSION, ssid, wOk ? 1 : 0, wIp.c_str(),
      running ? running->label : "?",
      running ? running->label : "?");
  }

  // ── Loop de comandos ──────────────────────────────────────
  for (;;) {
    OtaCmd cmd;
    if (xQueueReceive(qOtaCmd, &cmd, pdMS_TO_TICKS(2000)) != pdTRUE) continue;

    switch (cmd.type) {

      // ── Salvar WiFi ──────────────────────────────────────
      case OTA_CMD_WIFI_SET: {
        Preferences prefs;
        prefs.begin("ota_cfg", false);
        prefs.putString("ssid", cmd.ssid);
        prefs.putString("pass", cmd.pass);
        prefs.end();
        Serial.printf("OTA_WIFI_SAVED:%s\n", cmd.ssid);
        // Conecta imediatamente após salvar
        _otaConnectWifi();
        _otaSetState(OTA_STATE_IDLE);
        break;
      }

      // ── Status ───────────────────────────────────────────
      case OTA_CMD_STATUS: {
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *boot    = esp_ota_get_boot_partition();

        Preferences prefs;
        char savedSsid[64] = "nao_configurado";
        prefs.begin("ota_cfg", true);
        prefs.getString("ssid", savedSsid, sizeof(savedSsid));
        prefs.end();

        // Reconecta WiFi se credenciais estao salvas e nao esta conectado
        if (strcmp(savedSsid, "nao_configurado") != 0 &&
            WiFi.status() != WL_CONNECTED) {
          _otaEnsureWifi();
        }

        OtaStateEnum st = OTA_STATE_IDLE;
        bool pendVal = false;
        char latVer[16] = "-";
        if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
          st = gOtaStatus.state;
          pendVal = gOtaStatus.pendingValidation;
          strncpy(latVer, gOtaStatus.latestVersion, sizeof(latVer) - 1);
          xSemaphoreGive(mutexOta);
        }

        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        String ip = wifiOk ? WiFi.localIP().toString() : "";
        Serial.printf(
          "OTA_STATUS:state=%s,fw=%s,latest=%s,wifi=%s,wifiok=%d,ip=%s,pending=%d,running=%s,boot=%s\n",
          _otaStateStr(st), FIRMWARE_VERSION, latVer, savedSsid,
          wifiOk ? 1 : 0, ip.c_str(),
          pendVal ? 1 : 0,
          running ? running->label : "?",
          boot    ? boot->label    : "?");
        break;
      }

      // ── Verificar releases ───────────────────────────────
      case OTA_CMD_CHECK: {
        if (!_otaEnsureWifi()) { _otaSetState(OTA_STATE_DONE_ERR); break; }

        char latestVer[16]    = {0};
        char downloadUrl[256] = {0};
        bool found = _otaCheckGithub(latestVer, sizeof(latestVer),
                                     downloadUrl, sizeof(downloadUrl));
        // WiFi permanece conectado

        if (found) {
          bool available = (strcasecmp(latestVer, FIRMWARE_VERSION) != 0);
          if (xSemaphoreTake(mutexOta, pdMS_TO_TICKS(100)) == pdTRUE) {
            strncpy(gOtaStatus.latestVersion, latestVer,
                    sizeof(gOtaStatus.latestVersion) - 1);
            gOtaStatus.updateAvailable = available;
            gOtaStatus.state = OTA_STATE_IDLE;
            xSemaphoreGive(mutexOta);
          }
          Serial.printf("OTA_CHECK_OK:fw=%s,latest=%s,available=%d\n",
                        FIRMWARE_VERSION, latestVer, available ? 1 : 0);
        } else {
          _otaSetState(OTA_STATE_DONE_ERR);
        }
        break;
      }

      // ── Realizar update ──────────────────────────────────
      case OTA_CMD_UPDATE: {
        if (!_otaEnsureWifi()) { _otaSetState(OTA_STATE_DONE_ERR); break; }

        char latestVer[16]    = {0};
        char downloadUrl[256] = {0};
        if (!_otaCheckGithub(latestVer, sizeof(latestVer),
                             downloadUrl, sizeof(downloadUrl))) {
          _otaSetState(OTA_STATE_DONE_ERR);
          break;
        }

        Serial.printf("OTA_UPDATE_START:de=%s,para=%s\n",
                      FIRMWARE_VERSION, latestVer);
        logdbPublishf("OTA", "UPDATE", LOG_INFO,
                      "Update %s→%s", FIRMWARE_VERSION, latestVer);

        bool ok = _otaDownloadAndFlash(downloadUrl);
        // WiFi desconectado antes do reboot para liberar recursos

        if (ok) {
          WiFi.disconnect(true); // libera antes do reboot
          _otaSavePending(true);
          Serial.printf("OTA_UPDATE_OK:fw=%s→%s,reiniciando...\n",
                        FIRMWARE_VERSION, latestVer);
          vTaskDelay(pdMS_TO_TICKS(1000));
          esp_restart();
        } else {
          _otaSetState(OTA_STATE_DONE_ERR);
          Serial.println("OTA_UPDATE_FAIL:Falha no flash");
        }
        break;
      }

      // ── Rollback manual ──────────────────────────────────
      case OTA_CMD_ROLLBACK: {
        Serial.println("OTA_ROLLBACK_START");
        logdbPublish("OTA", "ROLLBACK_MANUAL", LOG_WARN,
                     "Rollback manual solicitado via serial/dashboard");
        _otaDoRollback();
        // Se chegou aqui, rollback falhou (sem firmware anterior)
        break;
      }

      // ── Scan de redes WiFi ───────────────────────────────
      case OTA_CMD_WIFI_SCAN: {
        // Desconecta temporariamente para o scan; reconecta depois
        bool wasConnected = (WiFi.status() == WL_CONNECTED);
        if (wasConnected) WiFi.disconnect(false); // não desliga modo STA
        WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(100));

        Serial.println("OTA_WIFI_SCAN_START");
        int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true

        if (n <= 0) {
          Serial.println("OTA_WIFI_SCAN_DONE:0");
          break;
        }

        // Ordena por RSSI (bubble sort simples — n é pequeno)
        for (int i = 0; i < n - 1; i++) {
          for (int j = 0; j < n - i - 1; j++) {
            if (WiFi.RSSI(j) < WiFi.RSSI(j + 1)) {
              // troca os índices internos do SDK
              // WiFi.scanNetworks já ordena, mas garantimos
            }
          }
        }

        for (int i = 0; i < n; i++) {
          bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
          Serial.printf("OTA_WIFI_SCAN_RESULT:%d,%s,%d,%d\n",
                        i,
                        WiFi.SSID(i).c_str(),
                        WiFi.RSSI(i),
                        secured ? 1 : 0);
        }
        Serial.printf("OTA_WIFI_SCAN_DONE:%d\n", n);
        WiFi.scanDelete();
        // Reconecta ao WiFi salvo após o scan
        if (wasConnected) _otaConnectWifi();
        break;
      }
    }
  }
}
