#pragma once

#include "lvgl.h"
#include "touch_orient.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the calibration UI if needed.
 *
 * Attempts to load a persisted touch orientation. If unavailable, it launches
 * the calibration wizard screen. When calibration exists, it is applied and
 * the function returns false to continue normal UI flow.
 *
 * @return true if the calibration screen was started and UI flow should wait.
 * @return false if calibration was applied or not required.
 */
bool ui_calibration_check_and_start(void);

/**
 * @brief Manually start the calibration screen.
 */
void ui_calibration_start(void);

/**
 * @brief Apply an existing calibration to the touch driver.
 */
void ui_calibration_apply(const touch_orient_config_t *cfg);

#ifdef __cplusplus
}
#endif
