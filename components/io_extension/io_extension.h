#pragma once
#include "esp_err.h"
#include "driver/i2c.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t io_ext_init(uint8_t i2c_addr, i2c_port_t port, SemaphoreHandle_t mutex);
esp_err_t io_ext_set(uint8_t pin, bool level);
esp_err_t io_ext_get(uint8_t pin, bool *level);
esp_err_t io_ext_pulse(uint8_t pin, uint32_t duration_ms);
