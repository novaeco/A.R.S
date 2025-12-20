#pragma once
#include "esp_err.h"
#include "lvgl.h"

esp_err_t display_driver_init(void);
void display_driver_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *color_map);
void display_driver_register_callbacks(lv_display_t *display);
