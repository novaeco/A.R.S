#pragma once

#include "board_pins.h"
#include "esp_err.h"
#include <stdbool.h>

esp_err_t board_init(void);
esp_err_t board_backlight_set(bool on);
esp_err_t board_lcd_power_set(bool on);
esp_err_t board_touch_reset_pulse(void);
esp_err_t board_sd_cs_set(bool asserted);
