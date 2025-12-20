#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOUCH_FOURCC_TO_U32(a, b, c, d)                                        \
  (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) |           \
   ((uint32_t)(uint8_t)(c) << 8) | ((uint32_t)(uint8_t)(d) << 0))

#define TOUCH_ORIENT_MAGIC 0x544F4348u // 'T', 'O', 'C', 'H'
#define TOUCH_ORIENT_VERSION 2

typedef struct {
  uint32_t magic;
  uint32_t version;
  bool swap_xy;
  bool mirror_x;
  bool mirror_y;
  float scale_x;
  float scale_y;
  int32_t offset_x;
  int32_t offset_y;
  uint32_t crc32;
} touch_orient_config_t;

/**
 * @brief Load orientation config from NVS
 *
 * @param[out] cfg Pointer to config struct to fill
 * @return ESP_OK if found and valid, ESP_ERR_NOT_FOUND if not in NVS,
 * ESP_ERR_INVALID_CRC if corrupt
 */
esp_err_t touch_orient_load(touch_orient_config_t *cfg);

/**
 * @brief Save orientation config to NVS
 *
 * @param[in] cfg Pointer to config struct to save
 * @return ESP_OK on success
 */
esp_err_t touch_orient_save(const touch_orient_config_t *cfg);

/**
 * @brief Apply configuration to the touch driver handle
 *
 * @param[in] tp Touch driver handle
 * @param[in] cfg Config to apply
 * @return ESP_OK on success
 */
esp_err_t touch_orient_apply(esp_lcd_touch_handle_t tp,
                             const touch_orient_config_t *cfg);

/**
 * @brief Return the last configuration applied at runtime.
 */
const touch_orient_config_t *touch_orient_get_active(void);

/**
 * @brief Apply orientation and clamp to a point.
 */
void touch_orient_map_point(const touch_orient_config_t *cfg, int32_t in_x,
                            int32_t in_y, int32_t max_x, int32_t max_y,
                            lv_point_t *out);

/**
 * @brief Helper to populate config from Kconfig defaults
 *
 * @param[out] cfg Pointer to config struct
 */
void touch_orient_get_defaults(touch_orient_config_t *cfg);

/**
 * @brief Remove the stored orientation profile from NVS (if present).
 *
 * This is useful to force regeneration from Kconfig defaults or
 * recalibration flows.
 */
esp_err_t touch_orient_clear(void);

#ifdef __cplusplus
}
#endif
