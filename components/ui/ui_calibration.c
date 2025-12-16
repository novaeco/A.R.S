#include "ui_calibration.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_helpers.h"
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include "ui_wizard.h"

static const char *TAG = "ui_calibration";

static void calibration_event_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Calibration completed, persisting setup flag");
  esp_err_t err = ui_wizard_mark_setup_done();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to persist setup_done flag: %s", esp_err_to_name(err));
  }
  ui_wizard_complete_from_calibration();
}

void ui_calibration_start(void) {
  ESP_LOGI(TAG, "Starting Calibration (UI stub)");

  lv_obj_t *scr_cal = lv_obj_create(NULL);
  ui_theme_apply(scr_cal);
  lv_obj_clear_flag(scr_cal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *header =
      ui_helper_create_header(scr_cal, "Calibration", NULL, NULL /* back */);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *body = lv_obj_create(scr_cal);
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT + UI_SPACE_MD);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_gap(body, UI_SPACE_LG, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(body);
  lv_label_set_text(title, "Calibration tactile");
  lv_obj_add_style(title, &ui_style_title, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);

  lv_obj_t *lbl = lv_label_create(body);
  lv_label_set_text(lbl,
                    "Suivez les instructions de calibration puis validez pour"
                    " poursuivre.");
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, LV_PCT(80));
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_style(lbl, &ui_style_text_body, 0);

  lv_obj_t *btn = lv_button_create(body);
  lv_obj_add_style(btn, &ui_style_btn_primary, 0);
  lv_obj_set_style_min_width(btn, 200, 0);
  lv_obj_set_style_min_height(btn, 56, 0);
  lv_obj_add_event_cb(btn, calibration_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "Valider");
  lv_obj_center(lbl_btn);

  ui_switch_screen(scr_cal, LV_SCR_LOAD_ANIM_NONE);
}
