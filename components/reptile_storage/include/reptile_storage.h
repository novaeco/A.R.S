#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>


// API
esp_err_t storage_nvs_set_str(const char *key, const char *value);
esp_err_t storage_nvs_get_str(const char *key, char *out_value, size_t max_len);
esp_err_t storage_nvs_set_i32(const char *key, int32_t value);
esp_err_t storage_nvs_get_i32(const char *key, int32_t *out_value);
