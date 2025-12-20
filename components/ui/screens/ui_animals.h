#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create and load the Animal List Screen.
 */
lv_obj_t *ui_create_animal_list_screen(void);

#ifdef __cplusplus
}
#endif