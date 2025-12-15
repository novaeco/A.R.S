#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create the Settings Screen (WiFi Config).
 */
lv_obj_t *ui_create_settings_screen(void);

#ifdef __cplusplus
}
#endif