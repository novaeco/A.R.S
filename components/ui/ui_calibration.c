#include "ui_calibration.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_wizard.h"


static const char *TAG = "ui_calibration";

static void calibration_event_cb(lv_event_t *e) {
  ESP_LOGI(TAG,
           "Calibration stub skipped/done. Proceeding to next wizard step.");
  // Notify wizard that calibration is completely done
  ui_wizard_next();
}

void ui_calibration_start(void) {
  ESP_LOGI(TAG, "Starting Calibration (Stub)");

  // Get active screen or create a temporary one if needed.
  // However, usually wizard manages screens. ui_wizard.c calls us.
  // Let's create a simple screen to show "Calibrating..." and a button to skip.

  lv_obj_t *scr_cal = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_cal, lv_color_black(), 0);

  lv_obj_t *label = lv_label_create(scr_cal);
  lv_label_set_text(label, "Touch Calibration\n(Stub Protocol)");
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *btn = lv_button_create(scr_cal);
  lv_obj_set_size(btn, 120, 50);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_event_cb(btn, calibration_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "SKIP / OK");
  lv_obj_center(lbl_btn);

  lv_screen_load(scr_cal);
}
