#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "lvgl.h"

// Spinner Control
void ui_helper_show_spinner(void);
void ui_helper_hide_spinner(void);

// Standard Header Creator
// parent: Screen object
// title: Title text
// back_cb: Function to call on back button click
// back_text: "Retour" or similar (if NULL, defaults to "Retour")
// return: Pointer to header object
lv_obj_t *ui_helper_create_header(lv_obj_t *parent, const char *title,
                                  lv_event_cb_t back_cb, const char *back_text);

// Standard Keyboard Setup (AZERTY)
void ui_helper_setup_keyboard(lv_obj_t *kb);

#endif
