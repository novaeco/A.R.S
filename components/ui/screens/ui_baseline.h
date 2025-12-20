#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_create_baseline_screen(void);
void ui_baseline_on_enter(void);
void ui_baseline_on_leave(void);

#ifdef __cplusplus
}
#endif
