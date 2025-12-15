#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize the main UI.
 */
void ui_init(void);

/**
 * @brief Create the dashboard screen (loaded via ui_switch_screen).
 */
lv_obj_t *ui_create_dashboard(void);

/**
 * @brief Function pointer type for getting battery level.
 * @param[out] percent Battery percentage (0-100)
 * @param[out] voltage_mv Voltage in mV
 * @return ESP_OK on success
 */
typedef int (*ui_battery_cb_t)(uint8_t *percent, uint16_t *voltage_mv);

/**
 * @brief Set the callback for retrieving battery level.
 * @param cb Callback function
 */
void ui_set_battery_cb(ui_battery_cb_t cb);

/**
 * @brief Wrapper to get battery level.
 */
int ui_get_battery_level(uint8_t *percent, uint16_t *voltage_mv);

#ifdef __cplusplus
}
#endif
