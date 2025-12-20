#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create the Animal Form Screen (Create or Edit).
 *
 * @param animal_id UUID of the animal to edit, or NULL to create new.
 */
lv_obj_t *ui_create_animal_form_screen(const char *animal_id);

#ifdef __cplusplus
}
#endif