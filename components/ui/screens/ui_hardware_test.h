#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_create_hardware_test_screen(void);
void ui_hardware_test_on_enter(void);
void ui_hardware_test_on_leave(void);

#ifdef __cplusplus
}
#endif
