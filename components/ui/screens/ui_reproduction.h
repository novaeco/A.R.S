#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create the Reproduction Management Screen for a specific animal.
 *
 * @param animal_id The UUID of the animal.
 */
lv_obj_t *ui_create_reproduction_screen(const char *animal_id);

#ifdef __cplusplus
}
#endif