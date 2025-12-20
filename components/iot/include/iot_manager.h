#pragma once

#include "esp_err.h"

void iot_init(void);

// Wi-Fi Control
esp_err_t iot_wifi_start(const char *ssid, const char *password);
esp_err_t iot_wifi_stop(void);

// OTA
// Launches an asynchronous OTA task using esp_https_ota().
// - url: HTTP/HTTPS firmware URL (not logged).
// Returns ESP_ERR_INVALID_STATE if an OTA is already running.
// On success, device restarts automatically after download/flash.
esp_err_t iot_ota_start(const char *url);
