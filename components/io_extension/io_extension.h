#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t io_extension_init(void);
esp_err_t io_extension_set_output(uint8_t pin, bool level);
esp_err_t io_extension_get_input(uint8_t pin, bool *level);
esp_err_t io_extension_pulse(uint8_t pin, uint32_t duration_ms);
