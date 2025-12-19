#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool swap_xy;
  bool mirror_x;
  bool mirror_y;
  float a11;
  float a12;
  float a13;
  float a21;
  float a22;
  float a23;
} touch_transform_t;

typedef struct {
  uint32_t magic;
  touch_transform_t transform;
  uint32_t version;
  uint32_t generation;
  uint32_t crc32;
} touch_transform_record_t;

typedef struct {
  float rms_error;
  float max_error;
  float condition_number;
  uint8_t points_used;
  uint8_t points_rejected;
} touch_transform_metrics_t;

typedef struct {
  bool pressed;
  uint16_t raw_x;
  uint16_t raw_y;
  uint64_t timestamp_us;
} touch_sample_raw_t;

void touch_transform_identity(touch_transform_t *out);

esp_err_t touch_transform_apply(const touch_transform_t *tf, int32_t raw_x,
                                int32_t raw_y, int32_t max_x, int32_t max_y,
                                lv_point_t *out);
esp_err_t touch_transform_apply_ex(const touch_transform_t *tf, int32_t raw_x,
                                   int32_t raw_y, int32_t max_x,
                                   int32_t max_y, bool apply_orientation,
                                   lv_point_t *out);

esp_err_t touch_transform_validate(const touch_transform_t *tf);

esp_err_t touch_transform_solve_affine(const lv_point_t *raw,
                                       const lv_point_t *ref, size_t count,
                                       touch_transform_t *out,
                                       touch_transform_metrics_t *metrics);

esp_err_t touch_transform_solve_fallback(const lv_point_t *raw,
                                         const lv_point_t *ref,
                                         size_t count, touch_transform_t *out);

const touch_transform_t *touch_transform_get_active(void);

void touch_transform_set_active(const touch_transform_t *tf);

touch_sample_raw_t touch_transform_sample_raw_oriented(
    esp_lcd_touch_handle_t tp, bool apply_orientation_hint);

esp_err_t touch_transform_storage_load(touch_transform_record_t *out);

esp_err_t touch_transform_storage_save(const touch_transform_record_t *rec);

esp_err_t touch_transform_storage_migrate_old(touch_transform_record_t *out);

/**
 * @brief Erase stored calibration/orientation slots from NVS.
 *
 * Useful to reset to build-time defaults or before running the calibration UI.
 */
esp_err_t touch_transform_storage_clear(void);

#ifdef __cplusplus
}
#endif

#ifndef TOUCH_FOURCC_TO_U32
#define TOUCH_FOURCC_TO_U32(a, b, c, d)                                        \
  (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) |           \
   ((uint32_t)(uint8_t)(c) << 8) | ((uint32_t)(uint8_t)(d) << 0))
#endif

#define TOUCH_TRANSFORM_MAGIC TOUCH_FOURCC_TO_U32('T', 'C', 'A', 'L')
#define TOUCH_TRANSFORM_VERSION 1

