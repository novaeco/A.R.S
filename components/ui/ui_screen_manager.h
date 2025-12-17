#ifndef UI_SCREEN_MANAGER_H
#define UI_SCREEN_MANAGER_H

#include "lvgl.h"

typedef enum {
  UI_TOAST_INFO = 0,
  UI_TOAST_SUCCESS,
  UI_TOAST_ERROR,
} ui_toast_type_t;

// Initialize the screen manager
void ui_screen_manager_init(void);

// Switch to a new screen with optional animation
void ui_switch_screen(lv_obj_t *screen, lv_scr_load_anim_t anim);

// Show/Hide a global loading overlay (spinner)
// Prevents interactions while active
void ui_show_loading(bool show);

// Show a temporary error toast/notification
void ui_show_error(const char *msg);

// Generic toast (info/success/error) with auto-dismiss
void ui_show_toast(const char *msg, ui_toast_type_t type);

// Guarded screen claim to enforce LVGL context and theme application
void ui_screen_claim_with_theme(lv_obj_t *screen, const char *name);

#endif
