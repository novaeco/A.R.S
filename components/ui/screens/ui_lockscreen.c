#include "screens/ui_lockscreen.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_screen_manager.h"
#include "lvgl.h"
#include "reptile_storage.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *ta_pin;
static char current_pin[8] = {0};

static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char *entered = lv_textarea_get_text(ta_pin);

    // Check PIN
    if (storage_nvs_get_str("sys_pin", current_pin, sizeof(current_pin)) !=
        ESP_OK) {
      // No PIN set, allow unlock
      ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
      return;
    }

    // If PIN stored is empty, allow unlock
    if (strlen(current_pin) == 0) {
      ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
      return;
    }

    if (strcmp(entered, current_pin) == 0) {
      ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
    } else {
      lv_textarea_set_text(ta_pin, "");
      ui_show_toast("Code PIN incorrect", UI_TOAST_ERROR);
    }
  }
}

lv_obj_t *ui_create_lockscreen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "Entrez le Code PIN");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);

  ta_pin = lv_textarea_create(scr);
  lv_textarea_set_password_mode(ta_pin, true);
  lv_textarea_set_one_line(ta_pin, true);
  lv_obj_set_width(ta_pin, 200);
  lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 90);

  // Audit Fix: Cleaned up dead button matrix code

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(kb, ta_pin);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL); // Catch READY

  return scr;
}