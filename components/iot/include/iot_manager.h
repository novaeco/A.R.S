#pragma once

#include "esp_err.h"

void iot_init(void);

// Wi-Fi Control
esp_err_t iot_wifi_start(const char *ssid, const char *password);
esp_err_t iot_wifi_stop(void);

// OTA
esp_err_t iot_ota_start(const char *url);
