/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "touch.h"
#include "gt911.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Function definitions
 *******************************************************************************/

/*******************************************************************************
 * Local variables
 *******************************************************************************/

/*******************************************************************************
 * Public API functions
 *******************************************************************************/

// Local calibration storage
static ars_touch_calibration_t s_calibration = {
    // GT911 already applies the Kconfig scale/offset (see gt911_apply_calibration),
    // so keep the runtime layer as an identity transform to avoid double-scaling.
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .offset_x = 0,
    .offset_y = 0,
};
static ars_touch_debug_info_t s_touch_debug_info = {0};
static portMUX_TYPE s_touch_debug_mux = portMUX_INITIALIZER_UNLOCKED;

void ars_touch_set_calibration(esp_lcd_touch_handle_t tp,
                               const ars_touch_calibration_t *data) {
  if (data) {
    s_calibration = *data;
  }
}

void ars_touch_get_calibration(esp_lcd_touch_handle_t tp,
                               ars_touch_calibration_t *data) {
  if (data) {
    *data = s_calibration;
  }
}

void ars_touch_apply_calibration(esp_lcd_touch_point_data_t *points,
                                 uint8_t count) {
  if (!points || count == 0)
    return;

  float scale_x = s_calibration.scale_x;
  float scale_y = s_calibration.scale_y;
  int32_t offset_x = s_calibration.offset_x;
  int32_t offset_y = s_calibration.offset_y;

  // Sanity check
  if (scale_x < 0.0001f || scale_x > 100.0f)
    scale_x = 1.0f;
  if (scale_y < 0.0001f || scale_y > 100.0f)
    scale_y = 1.0f;

  for (int i = 0; i < count; i++) {
    // Apply Scale
    if (scale_x > 0.001f) {
      points[i].x = (uint16_t)((float)points[i].x * scale_x);
    }
    if (scale_y > 0.001f) {
      points[i].y = (uint16_t)((float)points[i].y * scale_y);
    }

    // Apply Offset
    int32_t val_x = (int32_t)points[i].x + offset_x;
    int32_t val_y = (int32_t)points[i].y + offset_y;

    // Clamp
    if (val_x < 0)
      val_x = 0;
    if (val_y < 0)
      val_y = 0;

    // Note: We don't have x_max/y_max easily here unless we pass tp.
    // Assuming safe bounds or user sets correct offset.
    // For now, clamp to 2000 as per original logic if needed, but logic was:
    // if (val_x > 2000) continue;

    // Let's keep it simple and just assign, LVGL handles out of bounds usually.
    points[i].x = (uint16_t)val_x;
    points[i].y = (uint16_t)val_y;
  }
}

void ars_touch_debug_feed(int16_t raw_x, int16_t raw_y, int16_t x, int16_t y,
                          bool pressed) {
  gt911_stats_t stats = {0};
  gt911_get_stats(&stats);

  portENTER_CRITICAL(&s_touch_debug_mux);
  s_touch_debug_info.raw_x = raw_x;
  s_touch_debug_info.raw_y = raw_y;
  s_touch_debug_info.x = x;
  s_touch_debug_info.y = y;
  s_touch_debug_info.pressed = pressed;
  s_touch_debug_info.irq_total = stats.irq_total;
  s_touch_debug_info.empty_irqs = stats.empty_irqs;
  s_touch_debug_info.i2c_errors = stats.i2c_errors;
  s_touch_debug_info.polling = stats.polling_active;
  portEXIT_CRITICAL(&s_touch_debug_mux);
}

esp_err_t ars_touch_debug_get(ars_touch_debug_info_t *info) {
  if (!info) {
    return ESP_ERR_INVALID_ARG;
  }

  portENTER_CRITICAL(&s_touch_debug_mux);
  *info = s_touch_debug_info;
  portEXIT_CRITICAL(&s_touch_debug_mux);
  return ESP_OK;
}

void ars_touch_debug_reset(void) {
  gt911_reset_stats();
  portENTER_CRITICAL(&s_touch_debug_mux);
  memset(&s_touch_debug_info, 0, sizeof(s_touch_debug_info));
  portEXIT_CRITICAL(&s_touch_debug_mux);
}
