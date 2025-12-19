#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

typedef struct {
  int16_t raw_x;
  int16_t raw_y;
  int16_t x;
  int16_t y;
  uint32_t irq_total;
  uint32_t empty_irqs;
  uint32_t i2c_errors;
  uint32_t invalid_points;
  uint32_t clamped_points;
  uint16_t last_invalid_x;
  uint16_t last_invalid_y;
  bool pressed;
  bool polling;
} ars_touch_debug_info_t;

// ARS Custom Calibration Struct
typedef struct {
  float scale_x;
  float scale_y;
  int32_t offset_x;
  int32_t offset_y;
} ars_touch_calibration_t;

/**
 * @brief Set calibration data for the touch driver (Runtime)
 *
 * @param tp Touch handle (unused if single instance, but kept for API)
 * @param data Pointer to calibration data
 */
void ars_touch_set_calibration(esp_lcd_touch_handle_t tp,
                               const ars_touch_calibration_t *data);

/**
 * @brief Get current calibration data
 *
 * @param tp Touch handle
 * @param data Pointer to store calibration data
 */
void ars_touch_get_calibration(esp_lcd_touch_handle_t tp,
                               ars_touch_calibration_t *data);

/**
 * @brief Apply calibration to touch points (In-place)
 *
 * @param points Array of touch points
 * @param count Number of points
 */
void ars_touch_apply_calibration(esp_lcd_touch_point_data_t *points,
                                 uint8_t count);

void ars_touch_debug_feed(int16_t raw_x, int16_t raw_y, int16_t x, int16_t y,
                          bool pressed);
esp_err_t ars_touch_debug_get(ars_touch_debug_info_t *info);
void ars_touch_debug_reset(void);
