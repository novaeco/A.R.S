#include "iot_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <string.h>

#define DEFAULT_MQTT_URI "mqtt://test.mosquitto.org"
#define MAX_RETRY_COUNT 5

static const char *TAG = "iot";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static int s_retry_num = 0;
static bool s_wifi_initialized = false;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static void mqtt_app_start(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disconn =
        (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGW(TAG, "Wi-Fi Disconnected. Reason: %d", disconn->reason);

    // Check specific reason codes
    if (disconn->reason == WIFI_REASON_NO_AP_FOUND) {
      ESP_LOGE(TAG, "Reason 201: AP Not Found. Check SSID or Range.");
    } else if (disconn->reason == WIFI_REASON_AUTH_EXPIRE ||
               disconn->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
      ESP_LOGE(TAG, "Auth Failed. Check Password.");
    }

    /* Infinite Retry Logic or Long Wait */
    if (s_retry_num < MAX_RETRY_COUNT) {
      int delay_ms = 1000 * (s_retry_num + 1);
      ESP_LOGI(TAG, "Retrying connection (%d/%d) in %d ms...", s_retry_num + 1,
               MAX_RETRY_COUNT, delay_ms);
      vTaskDelay(pdMS_TO_TICKS(delay_ms));
      esp_wifi_connect();
      s_retry_num++;
    } else {
      /* After Max Retries, wait longer then reset counter to keep trying */
      ESP_LOGW(TAG, "Max retries reached. Waiting 30s before next attempt...");
      vTaskDelay(pdMS_TO_TICKS(30000));
      s_retry_num = 0;
      esp_wifi_connect();
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    mqtt_app_start();
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG, "MQTT Event: %" PRIi32, event_id);
}

static void mqtt_app_start(void) {
  if (s_mqtt_client)
    return; // Already started

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = DEFAULT_MQTT_URI,
  };
  s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, s_mqtt_client);
  esp_mqtt_client_start(s_mqtt_client);
  ESP_LOGI(TAG, "MQTT Client Started");
}

void iot_init(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize Netif and Event Loop
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  (void)sta_netif;

  s_wifi_event_group = xEventGroupCreate();

  // Register Event Handlers
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  ESP_LOGI(TAG, "IoT Core Initialized.");

#ifdef CONFIG_IOT_WIFI_ENABLED
  ESP_LOGI(TAG, "Wi-Fi Enabled in Kconfig. Starting...");
  iot_wifi_start(CONFIG_IOT_WIFI_SSID, CONFIG_IOT_WIFI_PASSWORD);
#else
  ESP_LOGI(TAG, "Wi-Fi Disabled in Kconfig. To enable: 'idf.py menuconfig' -> "
                "'Project Configuration' -> 'Enable Wi-Fi'");
#endif
}

esp_err_t iot_wifi_start(const char *ssid, const char *password) {
  if (s_wifi_initialized) {
    ESP_LOGW(TAG, "Wi-Fi already initialized. Re-connecting...");
    esp_wifi_stop();
  }

  // Check if SSID is provided
  if (ssid == NULL || strlen(ssid) == 0) {
    ESP_LOGE(TAG, "No SSID provided!");
    return ESP_FAIL;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {0};
  strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, password ? password : "",
          sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  s_retry_num = 0;
  ESP_ERROR_CHECK(esp_wifi_start());
  s_wifi_initialized = true;

  ESP_LOGI(TAG, "Wi-Fi Started. Connecting to %s...", ssid);
  return ESP_OK;
}

esp_err_t iot_wifi_stop(void) {
  if (!s_wifi_initialized)
    return ESP_OK;

  ESP_LOGI(TAG, "Stopping Wi-Fi...");

  if (s_mqtt_client) {
    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
  }

  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();

  s_wifi_initialized = false;
  ESP_LOGI(TAG, "Wi-Fi De-initialized.");
  return ESP_OK;
}

esp_err_t iot_ota_start(const char *url) {
  ESP_LOGI(TAG, "Stub: Starting OTA from %s", url);
  return ESP_OK;
}
