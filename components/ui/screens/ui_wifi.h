#pragma once
#include "lvgl.h"

extern lv_obj_t *ui_ScreenWifi;

lv_obj_t *ui_create_screen_wifi(void);
void ui_wifi_on_enter(void);
void ui_wifi_on_leave(void);
