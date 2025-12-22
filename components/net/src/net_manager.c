#include "net_manager.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "web_server.h"
#include <inttypes.h>
#include <string.h>
#include <strings.h>

#ifndef CONFIG_ARS_ENABLE_HTTP_CLIENT
#define CONFIG_ARS_ENABLE_HTTP_CLIENT 0
#endif

#ifndef CONFIG_ARS_HTTP_CLIENT_ALLOW_INSECURE
#define CONFIG_ARS_HTTP_CLIENT_ALLOW_INSECURE 0
#endif

// Internal State
static net_status_t s_net_status = {.is_connected = false,
                                    .got_ip = false,
                                    .rssi = 0,
                                    .ip_addr = {0},
                                    .last_error = ESP_OK};
static wifi_prov_state_t s_prov_state = WIFI_PROV_STATE_NOT_PROVISIONED;
static wifi_err_reason_t s_last_reason = WIFI_REASON_UNSPECIFIED;
static bool s_logged_waiting = false;

static const char *TAG = "NET";
static const char *NVS_NAMESPACE = "net";
static const char *NVS_KEY_WIFI_SSID = "wifi_ssid";
static const char *NVS_KEY_WIFI_PASS = "wifi_pass";

static bool is_connected = false;
static bool has_credentials = false;
static bool s_wifi_started = false;
static esp_timer_handle_t s_wifi_retry_timer = NULL;
static uint32_t s_wifi_backoff_ms = 1000; // start at 1s
static const uint32_t WIFI_RETRY_BACKOFF_MAX_MS = 30000;
static const uint32_t WIFI_RETRY_MAX_ATTEMPTS = 8;
static uint32_t s_wifi_retry_count = 0;
static TaskHandle_t s_wifi_retry_task = NULL;
static TaskHandle_t s_wifi_provisioning_task = NULL;
static bool s_watchdog_task_created = false;
static void schedule_wifi_retry(uint32_t delay_ms);
static esp_err_t start_wifi_station_if_provisioned(void);
static void wifi_provisioning_task(void *arg);
ESP_EVENT_DEFINE_BASE(NET_MANAGER_EVENT);

static wifi_prov_state_t disc_reason_to_state(wifi_err_reason_t reason,
                                              wifi_err_reason_t *out_reason) {
  wifi_prov_state_t state = WIFI_PROV_STATE_FAILED;
  switch (reason) {
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_AUTH_EXPIRE:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    state = WIFI_PROV_STATE_WRONG_PASSWORD;
    break;
  case WIFI_REASON_NO_AP_FOUND:
    state = WIFI_PROV_STATE_FAILED;
    break;
  default:
    break;
  }
  if (out_reason)
    *out_reason = reason;
  return state;
}

static void net_manager_update_state(wifi_prov_state_t state,
                                     wifi_err_reason_t reason) {
  if (state == s_prov_state && reason == s_last_reason)
    return;
  s_prov_state = state;
  s_last_reason = reason;
  net_manager_state_evt_t evt = {.state = state, .reason = reason};
  esp_err_t post_err =
      esp_event_post(NET_MANAGER_EVENT, NET_MANAGER_EVENT_STATE_CHANGED, &evt,
                     sizeof(evt), portMAX_DELAY);
  if (post_err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to emit provisioning state event: %s",
             esp_err_to_name(post_err));
  }
}

static esp_err_t
apply_sta_config_with_recovery(const wifi_config_t *wifi_config_in) {
  if (wifi_config_in == NULL) {
    ESP_LOGE(TAG, "Cannot apply STA config: null input");
    return ESP_ERR_INVALID_ARG;
  }

  wifi_mode_t current_mode = WIFI_MODE_NULL;
  esp_err_t err = esp_wifi_get_mode(&current_mode);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
    // Continue; recovery below will reset mode as needed
  }

  wifi_config_t cfg = *wifi_config_in;

  ESP_LOGI(TAG, "Applying STA config (mode=%d)", (int)current_mode);

  err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  if (err == ESP_ERR_WIFI_MODE) {
    ESP_LOGW(TAG, "esp_wifi_set_config returned ESP_ERR_WIFI_MODE (mode=%d)",
             (int)current_mode);

    esp_wifi_disconnect();
    esp_wifi_stop();

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set STA mode during recovery: %s",
               esp_err_to_name(err));
      return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start Wi-Fi during recovery: %s",
               esp_err_to_name(err));
      return err;
    }
    s_wifi_started = true;

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to apply STA config: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "STA config applied successfully");

  return ESP_OK;
}

static void wifi_retry_execute(void) {
  ESP_LOGI(TAG, "Wi-Fi reconnect attempt (backoff %ums)", s_wifi_backoff_ms);
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_connect failed during retry: %s",
             esp_err_to_name(err));
  }
}

static void wifi_retry_task(void *arg) {
  (void)arg;
  // Register with Task WDT for monitoring
  esp_task_wdt_add(NULL);
  while (1) {
    esp_task_wdt_reset();
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    wifi_retry_execute();
  }
}

static void wifi_retry_timer_cb(void *arg) {
  (void)arg;
  if (s_wifi_retry_task) {
    xTaskNotifyGive(s_wifi_retry_task);
  }
}

static esp_err_t ensure_wifi_retry_timer(void) {
  if (s_wifi_retry_timer) {
    return ESP_OK;
  }

  const esp_timer_create_args_t tmr_args = {.callback = wifi_retry_timer_cb,
                                            .name = "wifi_retry"};
  esp_err_t err = esp_timer_create(&tmr_args, &s_wifi_retry_timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create Wi-Fi retry timer: %s",
             esp_err_to_name(err));
  }
  return err;
}

static esp_err_t ensure_wifi_retry_task(void) {
  if (s_wifi_retry_task) {
    return ESP_OK;
  }

  BaseType_t task_ok = xTaskCreatePinnedToCore(
      wifi_retry_task, "wifi_retry_task", 4096, NULL, 4, &s_wifi_retry_task, 0);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Wi-Fi retry task");
    return ESP_FAIL;
  }
  return ESP_OK;
}

static void wifi_watchdog_task(void *arg) {
  (void)arg;
  // Register with Task WDT for monitoring
  esp_task_wdt_add(NULL);
  while (1) {
    esp_task_wdt_reset();
    if (has_credentials && !is_connected) {
      ESP_LOGW(TAG, "Watchdog: WiFi disconnected, triggering retry logic...");
      schedule_wifi_retry(5);
    }
    vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10s
  }
}

static void ensure_watchdog_task(void) {
  if (s_watchdog_task_created) {
    return;
  }

  if (xTaskCreate(wifi_watchdog_task, "net_wdog", 2048, NULL, 1, NULL) ==
      pdPASS) {
    s_watchdog_task_created = true;
  } else {
    ESP_LOGE(TAG, "Failed to create Wi-Fi watchdog task");
  }
}

// Replaces static void stop_wifi_retry_timer_best_effort(void) ...
// (Wait, I should not delete stop_wifi_retry_timer_best_effort, I am replacing
// a chunk. I will insert before it)
static void stop_wifi_retry_timer_best_effort(void) {
  if (!s_wifi_retry_timer) {
    return;
  }

  esp_err_t err = esp_timer_stop(s_wifi_retry_timer);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "Wi-Fi retry timer was not running");
    return;
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to stop Wi-Fi retry timer: %s", esp_err_to_name(err));
  }
}

static void initialize_sntp(void) {
  ESP_LOGI(TAG, "Initializing SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
}

static esp_err_t start_wifi_station_if_provisioned(void) {
  if (!has_credentials) {
    if (!s_logged_waiting) {
      ESP_LOGW(TAG, "Wi-Fi credentials missing, station start skipped");
      s_logged_waiting = true;
    }
    net_manager_update_state(WIFI_PROV_STATE_NOT_PROVISIONED,
                             WIFI_REASON_UNSPECIFIED);
    return ESP_ERR_INVALID_STATE;
  }

  if (s_wifi_started) {
    return ESP_OK;
  }

  esp_err_t err = esp_wifi_start();
  if (err == ESP_ERR_WIFI_CONN) {
    s_wifi_started = true;
    return ESP_OK; // Already started
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
    net_manager_update_state(WIFI_PROV_STATE_FAILED, WIFI_REASON_UNSPECIFIED);
    return err;
  }

  s_wifi_started = true;
  net_manager_update_state(WIFI_PROV_STATE_CONNECTING, WIFI_REASON_UNSPECIFIED);
  return ESP_OK;
}

static void schedule_wifi_retry(uint32_t delay_ms) {
  if (!has_credentials) {
    ESP_LOGW(TAG, "Wi-Fi credentials not set, waiting...");
    return;
  }

  if (ensure_wifi_retry_task() != ESP_OK) {
    return;
  }

  if (ensure_wifi_retry_timer() != ESP_OK) {
    return;
  }

  stop_wifi_retry_timer_best_effort();

  if (s_wifi_retry_count >= WIFI_RETRY_MAX_ATTEMPTS) {
    ESP_LOGW(TAG, "Wi-Fi retry limit reached (%u attempts)",
             WIFI_RETRY_MAX_ATTEMPTS);
    return;
  }
  s_wifi_retry_count++;

  esp_err_t err = esp_timer_start_once(s_wifi_retry_timer, delay_ms * 1000ULL);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to schedule Wi-Fi retry: %s", esp_err_to_name(err));
  }
}

static void wifi_provisioning_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "Provisioning: ouvrez Parametres -> Wi-Fi et saisissez les "
                "identifiants.");
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!has_credentials) {
      continue;
    }

    ESP_LOGI(TAG, "Provisioning: credentials stored, starting STA bring-up");
    esp_err_t start_err = start_wifi_station_if_provisioned();
    if (start_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start Wi-Fi after provisioning: %s",
               esp_err_to_name(start_err));
      continue;
    }

    if (CONFIG_ARS_WIFI_AUTOCONNECT) {
      ensure_wifi_retry_task();
      ensure_watchdog_task();
      schedule_wifi_retry(10);
    } else {
      ESP_LOGI(TAG,
               "Autoconnect disabled; waiting for manual connection request");
    }
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Wi-Fi STA start");
    if (!has_credentials) {
      ESP_LOGW(TAG, "WiFi not provisioned, STA connect skipped");
      return;
    }
    if (!CONFIG_ARS_WIFI_AUTOCONNECT) {
      ESP_LOGI(TAG, "Wi-Fi autoconnect disabled; waiting for manual connect");
      return;
    }
    net_manager_update_state(WIFI_PROV_STATE_CONNECTING,
                             WIFI_REASON_UNSPECIFIED);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "esp_wifi_connect failed on STA start: %s",
               esp_err_to_name(err));
      schedule_wifi_retry(s_wifi_backoff_ms);
    }
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    is_connected = false;
    s_net_status.is_connected = false;
    s_net_status.got_ip = false;
    memset(s_net_status.ip_addr, 0, sizeof(s_net_status.ip_addr));

    ESP_LOGW(TAG, "Network Link Down - Stopping Services");
    web_server_stop();
    wifi_event_sta_disconnected_t *disc =
        (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d).", disc ? disc->reason : -1);
    wifi_err_reason_t reason = disc ? disc->reason : WIFI_REASON_UNSPECIFIED;
    net_manager_update_state(disc_reason_to_state(reason, &reason), reason);

    if (CONFIG_ARS_WIFI_AUTOCONNECT) {
      // Exponential backoff capped at 30s
      if (s_wifi_backoff_ms < WIFI_RETRY_BACKOFF_MAX_MS) {
        uint32_t next_backoff = s_wifi_backoff_ms * 2;
        s_wifi_backoff_ms = next_backoff > WIFI_RETRY_BACKOFF_MAX_MS
                                ? WIFI_RETRY_BACKOFF_MAX_MS
                                : next_backoff;
      }
      schedule_wifi_retry(s_wifi_backoff_ms);
    } else {
      ESP_LOGW(TAG, "Autoconnect disabled; manual reconnection required");
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    // Update Status
    is_connected = true;
    s_net_status.is_connected = true;
    s_net_status.got_ip = true;
    snprintf(s_net_status.ip_addr, sizeof(s_net_status.ip_addr), IPSTR,
             IP2STR(&event->ip_info.ip));

    ESP_LOGI(TAG, "Got IP: %s", s_net_status.ip_addr);

    s_wifi_backoff_ms = 1000; // reset backoff after success
    s_wifi_retry_count = 0;
    stop_wifi_retry_timer_best_effort();

    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      s_net_status.rssi = ap_info.rssi;
      ESP_LOGI(TAG, "Connected SSID=%s RSSI=%d dBm", (char *)ap_info.ssid,
               ap_info.rssi);
    }

    ESP_LOGI(TAG, "Network Ready - Starting Services");

    net_manager_update_state(WIFI_PROV_STATE_CONNECTED,
                             WIFI_REASON_UNSPECIFIED);

    // Init SNTP only after getting IP
    if (!esp_sntp_enabled()) {
      initialize_sntp();
    }

    web_server_init(); // Start server on connect
  }
}

static esp_err_t net_load_credentials_from_nvs(char *ssid, size_t ssid_len,
                                               char *pass, size_t pass_len) {
  nvs_handle_t nvs = 0;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    return err;
  }

  size_t ssid_required = 0;
  size_t pass_required = 0;
  err = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, NULL, &ssid_required);
  if (err == ESP_OK && ssid_required > ssid_len) {
    err = ESP_ERR_NVS_INVALID_LENGTH;
  }
  if (err == ESP_OK) {
    err = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, ssid, &ssid_required);
  }
  if (err == ESP_OK) {
    err = nvs_get_str(nvs, NVS_KEY_WIFI_PASS, NULL, &pass_required);
    if (err == ESP_OK && pass_required > pass_len) {
      err = ESP_ERR_NVS_INVALID_LENGTH;
    }
  }
  if (err == ESP_OK) {
    err = nvs_get_str(nvs, NVS_KEY_WIFI_PASS, pass, &pass_required);
  }

  nvs_close(nvs);
  if (err != ESP_OK) {
    if (ssid_len > 0) {
      ssid[0] = '\0';
    }
    if (pass_len > 0) {
      pass[0] = '\0';
    }
  }
  return err;
}

static bool net_has_nonempty_credentials(const wifi_config_t *cfg) {
  return cfg && cfg->sta.ssid[0] != '\0';
}

esp_err_t net_manager_set_credentials(const char *ssid, const char *password,
                                      bool persist) {
  if (!ssid || !password) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t ssid_len = strlen(ssid);
  size_t pass_len = strlen(password);
  if (ssid_len == 0 || pass_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (ssid_len >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
      pass_len >= sizeof(((wifi_config_t *)0)->sta.password)) {
    return ESP_ERR_INVALID_ARG;
  }

  if (persist) {
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
      return err;
    }
    err = nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) {
      err = nvs_set_str(nvs, NVS_KEY_WIFI_PASS, password);
    }
    if (err == ESP_OK) {
      err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
      return err;
    }
  }

  wifi_config_t wifi_cfg = {0};
  strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
  strlcpy((char *)wifi_cfg.sta.password, password,
          sizeof(wifi_cfg.sta.password));

  esp_err_t err = apply_sta_config_with_recovery(&wifi_cfg);
  if (err == ESP_OK) {
    has_credentials = true;
    s_logged_waiting = false;
    net_manager_update_state(WIFI_PROV_STATE_CONNECTING,
                             WIFI_REASON_UNSPECIFIED);
    s_wifi_backoff_ms = 1000;
    s_wifi_retry_count = 0;
    esp_err_t start_err = start_wifi_station_if_provisioned();
    if (start_err == ESP_OK && CONFIG_ARS_WIFI_AUTOCONNECT) {
      ensure_wifi_retry_task();
      ensure_watchdog_task();
      if (!s_wifi_provisioning_task) {
        schedule_wifi_retry(10);
      }
    } else if (start_err == ESP_OK) {
      esp_err_t connect_err = esp_wifi_connect();
      if (connect_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect (manual) failed: %s",
                 esp_err_to_name(connect_err));
      }
    }
    if (s_wifi_provisioning_task) {
      xTaskNotifyGive(s_wifi_provisioning_task);
    }
  }
  if (err != ESP_OK) {
    net_manager_update_state(WIFI_PROV_STATE_FAILED, WIFI_REASON_UNSPECIFIED);
  }
  return err;
}

static bool net_try_load_credentials(wifi_config_t *wifi_cfg) {
  char ssid[sizeof(wifi_cfg->sta.ssid)] = {0};
  char pass[sizeof(wifi_cfg->sta.password)] = {0};

  if (CONFIG_ARS_WIFI_USE_NVS) {
    esp_err_t err =
        net_load_credentials_from_nvs(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err == ESP_OK && ssid[0] != '\0') {
      ESP_LOGI(TAG, "Using WiFi credentials from NVS (namespace '%s')",
               NVS_NAMESPACE);
      strlcpy((char *)wifi_cfg->sta.ssid, ssid, sizeof(wifi_cfg->sta.ssid));
      strlcpy((char *)wifi_cfg->sta.password, pass,
              sizeof(wifi_cfg->sta.password));
      return true;
    }
  }

  if (strlen(CONFIG_ARS_WIFI_SSID) > 0) {
    ESP_LOGI(TAG, "Using WiFi credentials from Kconfig (CONFIG_ARS_WIFI_SSID)");
    strlcpy((char *)wifi_cfg->sta.ssid, CONFIG_ARS_WIFI_SSID,
            sizeof(wifi_cfg->sta.ssid));
    strlcpy((char *)wifi_cfg->sta.password, CONFIG_ARS_WIFI_PASSWORD,
            sizeof(wifi_cfg->sta.password));
    return true;
  }

  return false;
}

esp_err_t net_init(void) {
  ESP_LOGI(TAG, "Initializing Network...");

  // Yield briefly to reset watchdog and allow other tasks to run
  vTaskDelay(pdMS_TO_TICKS(5));

  // Note: NVS, Netif, and Event Loop are now initialized in app_main()
  // to ensure they are ready before any component usage.
  esp_err_t err = ESP_OK;

  err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
    return err;
  }

  if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
    esp_netif_create_default_wifi_sta();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
      ESP_LOGE(TAG, "NVS not initialized! Ensure nvs_flash_init() is called in "
                    "app_main.");
    }
    ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Yield after WiFi init to allow watchdog reset
  vTaskDelay(pdMS_TO_TICKS(10));

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    return err;
  }

  wifi_config_t wifi_config = {0};
  has_credentials = net_try_load_credentials(&wifi_config) &&
                    net_has_nonempty_credentials(&wifi_config);
  if (has_credentials) {
    ESP_LOGI(TAG, "Applying provisioned Wi-Fi credentials (SSID='%s')",
             (char *)wifi_config.sta.ssid);
    if (apply_sta_config_with_recovery(&wifi_config) != ESP_OK) {
      has_credentials = false;
      net_manager_update_state(WIFI_PROV_STATE_NOT_PROVISIONED,
                               WIFI_REASON_UNSPECIFIED);
    }
  } else {
    // Provisioning mode: show clear instructions for the UI flow
    ESP_LOGI(TAG,
             "Wi-Fi not provisioned. UI -> Parametres -> Wi-Fi pour saisir "
             "SSID/Mot de passe. Provisioning task waiting...");
    net_manager_update_state(WIFI_PROV_STATE_NOT_PROVISIONED,
                             WIFI_REASON_UNSPECIFIED);
  }

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &event_handler, NULL, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Handler register failed: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &event_handler, NULL, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Handler register failed: %s", esp_err_to_name(err));
    return err;
  }

  // Start Wi-Fi only when provisioned; otherwise stay idle and wait for
  // provisioning task notification to avoid blind connection attempts.

  if (has_credentials) {
    ESP_LOGI(TAG, "Starting Wi-Fi station (provisioned path)...");
    err = start_wifi_station_if_provisioned();
    if (err != ESP_OK) {
      return err;
    }
  } else if (!s_wifi_provisioning_task) {
    BaseType_t task_ok = xTaskCreate(wifi_provisioning_task, "wifi_prov", 4096,
                                     NULL, 4, &s_wifi_provisioning_task);
    if (task_ok != pdPASS) {
      ESP_LOGE(TAG, "Failed to start Wi-Fi provisioning task");
    }
  }

  // FIX C: Check Autoconnect config
  if (has_credentials && CONFIG_ARS_WIFI_AUTOCONNECT) {
    // Connect is triggered by WIFI_EVENT_STA_START in event_handler usually,
    // but we need to ensure we don't loop if not configured.
    // logic in event_handler: if (WIFI_EVENT_STA_START) { if (!has_credentials)
    // return; ... } This is safe.
  }

  if (has_credentials && CONFIG_ARS_WIFI_AUTOCONNECT) {
    ESP_RETURN_ON_ERROR(ensure_wifi_retry_task(), TAG,
                        "failed to start Wi-Fi retry worker");
    ensure_watchdog_task();
  }

  return ESP_OK;
}

esp_err_t net_connect(const char *ssid, const char *password) {
  ESP_LOGI(TAG, "Connecting to %s...", ssid);
  return net_manager_set_credentials(ssid, password, false);
}

bool net_is_connected(void) { return is_connected; }

bool net_manager_is_provisioned(void) { return has_credentials; }

wifi_prov_state_t net_manager_get_prov_state(void) { return s_prov_state; }

wifi_err_reason_t net_manager_get_last_reason(void) { return s_last_reason; }

esp_err_t net_manager_forget_credentials(void) {
  nvs_handle_t nvs = 0;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    nvs_erase_key(nvs, NVS_KEY_WIFI_SSID);
    nvs_erase_key(nvs, NVS_KEY_WIFI_PASS);
    nvs_commit(nvs);
    nvs_close(nvs);
  }

  stop_wifi_retry_timer_best_effort();
  s_wifi_backoff_ms = 1000;
  if (s_wifi_started) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_started = false;
  }
  is_connected = false;
  s_net_status.is_connected = false;
  s_net_status.got_ip = false;
  memset(s_net_status.ip_addr, 0, sizeof(s_net_status.ip_addr));
  has_credentials = false;
  s_logged_waiting = false;
  net_manager_update_state(WIFI_PROV_STATE_NOT_PROVISIONED,
                           WIFI_REASON_UNSPECIFIED);
  return err;
}

net_status_t net_get_status(void) { return s_net_status; }

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
  // ... (Same as before, minimal implementation)
  return ESP_OK;
}

esp_err_t net_http_get(const char *url, char *out_buffer, size_t buffer_len) {
#if !CONFIG_ARS_ENABLE_HTTP_CLIENT
  ESP_LOGW(TAG,
           "HTTP client disabled by Kconfig (CONFIG_ARS_ENABLE_HTTP_CLIENT=n)");
  return ESP_ERR_NOT_SUPPORTED;
#endif

  if (!url || !out_buffer || buffer_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!is_connected) {
    return ESP_ERR_INVALID_STATE;
  }

  bool is_https = false;
  if (url != NULL && strncasecmp(url, "https://", 8) == 0) {
    is_https = true;
  }

  if (!is_https && !CONFIG_ARS_HTTP_CLIENT_ALLOW_INSECURE) {
    ESP_LOGW(TAG, "Insecure HTTP (non-TLS) is disabled by Kconfig");
    return ESP_ERR_NOT_SUPPORTED;
  }

  esp_http_client_config_t config = {
      .url = url,
      .event_handler = _http_event_handler,
      .timeout_ms = 5000,
      .buffer_size = 1024,
  };

  if (is_https) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  const int64_t content_length = esp_http_client_fetch_headers(client);
  size_t total_read = 0;
  bool truncated = false;

  while (true) {
    if (total_read >= buffer_len - 1) {
      truncated = true;
      break;
    }

    int to_read = (int)(buffer_len - 1 - total_read);
    int read_len =
        esp_http_client_read(client, out_buffer + total_read, to_read);
    if (read_len < 0) {
      ESP_LOGE(TAG, "HTTP read failed after %u bytes: %s", (unsigned)total_read,
               esp_err_to_name(read_len));
      err = ESP_FAIL;
      break;
    }
    if (read_len == 0) {
      break; // EOF
    }

    total_read += (size_t)read_len;

    if (content_length >= 0 && total_read >= (size_t)content_length) {
      break; // Completed expected length
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (err == ESP_OK) {
    out_buffer[total_read] = '\0';
    if (truncated) {
      ESP_LOGW(TAG,
               "HTTP response truncated to %u bytes (buffer size %u, content "
               "length %" PRId64 ")",
               (unsigned)total_read, (unsigned)buffer_len, content_length);
      err = ESP_ERR_NO_MEM;
    }
  } else if (buffer_len > 0) {
    out_buffer[0] = '\0';
  }

  return err;
}

esp_err_t net_http_client_init_secure(esp_http_client_handle_t *client,
                                      const char *url) {
  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 10000,
  };
  *client = esp_http_client_init(&config);
  return *client ? ESP_OK : ESP_FAIL;
}

// Helper for future HTTPS client usage
void net_demo_https(void) {
  esp_http_client_handle_t client;
  if (net_http_client_init_secure(&client, "https://www.google.com") ==
      ESP_OK) {
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
  }
}