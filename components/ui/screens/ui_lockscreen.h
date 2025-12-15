#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create the Lock Screen.
 * Blocks access until correct PIN is entered.
 */
lv_obj_t *ui_create_lockscreen(void);

#ifdef __cplusplus
}
#endif