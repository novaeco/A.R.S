#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t lvgl_port_init(void);
lv_display_t *lvgl_port_get_display(void);
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
