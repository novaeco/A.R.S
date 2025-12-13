#ifndef UI_SCREEN_MANAGER_H
#define UI_SCREEN_MANAGER_H

#include "lvgl.h"

// Initialize the screen manager
void ui_screen_manager_init(void);

// Switch to a new screen with optional animation
void ui_switch_screen(lv_obj_t *screen, lv_scr_load_anim_t anim);

// Show/Hide a global loading overlay (spinner)
// Prevents interactions while active
void ui_show_loading(bool show);

// Show a temporary error toast/notification
void ui_show_error(const char *msg);

#endif
