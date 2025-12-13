#include "ui_wizard.h"
#include "esp_log.h"
#include "lvgl.h"
#include "net_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "screens/ui_wifi.h"
#include "ui.h"
#include "ui_calibration.h"

static const char *TAG = "ui_wizard";

typedef enum {
  WIZARD_STEP_INIT,
  WIZARD_STEP_CALIBRATION,
  WIZARD_STEP_WIFI_ASK,
  WIZARD_STEP_WIFI_CONFIG,
  WIZARD_STEP_DONE
} wizard_step_t;

static wizard_step_t current_step = WIZARD_STEP_INIT;
static lv_obj_t *scr_ask_wifi = NULL;

static void save_setup_done(void) {
  nvs_handle_t handle;
  if (nvs_open("system", NVS_READWRITE, &handle) == ESP_OK) {
    uint8_t done = 1;
    nvs_set_u8(handle, "setup_done", done);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Setup marked as done in NVS.");
  }
}

// -- Wi-Fi Ask Screen Helper --
static void ask_wifi_event_cb(lv_event_t *e) {
  long user_choice = (long)lv_event_get_user_data(e);
  if (user_choice == 1) {
    // YES -> Config
    current_step = WIZARD_STEP_WIFI_CONFIG;
    ui_wizard_next();
  } else {
    // NO -> Done
    current_step = WIZARD_STEP_DONE;
    ui_wizard_next();
  }
}

static void create_ask_wifi_screen(void) {
  if (scr_ask_wifi)
    lv_obj_del(scr_ask_wifi);
  scr_ask_wifi = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_ask_wifi, lv_color_black(), 0);

  lv_obj_t *lbl = lv_label_create(scr_ask_wifi);
  lv_label_set_text(lbl, "Do you want to configure\nWi-Fi now?");
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

  // YES Button
  lv_obj_t *btn_yes = lv_button_create(scr_ask_wifi);
  lv_obj_set_size(btn_yes, 100, 50);
  lv_obj_align(btn_yes, LV_ALIGN_CENTER, -60, 40);
  lv_obj_add_event_cb(btn_yes, ask_wifi_event_cb, LV_EVENT_CLICKED, (void *)1);
  lv_obj_t *lbl_yes = lv_label_create(btn_yes);
  lv_label_set_text(lbl_yes, "YES");
  lv_obj_center(lbl_yes);

  // NO Button
  lv_obj_t *btn_no = lv_button_create(scr_ask_wifi);
  lv_obj_set_size(btn_no, 100, 50);
  lv_obj_align(btn_no, LV_ALIGN_CENTER, 60, 40);
  lv_obj_set_style_bg_color(btn_no, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btn_no, ask_wifi_event_cb, LV_EVENT_CLICKED, (void *)0);
  lv_obj_t *lbl_no = lv_label_create(btn_no);
  lv_label_set_text(lbl_no, "NO");
  lv_obj_center(lbl_no);

  lv_screen_load(scr_ask_wifi);
}

void ui_wizard_next(void) {
  ESP_LOGI(TAG, "Wizard transition from step %d", current_step);

  switch (current_step) {
  case WIZARD_STEP_INIT:
    // Should not happen if started via ui_wizard_start
    current_step = WIZARD_STEP_CALIBRATION;
    break;

  case WIZARD_STEP_CALIBRATION:
    // This function is called WHEN calibration is done (or if we skipped to it)
    // But we actually want to START calibration here if we just arrived.
    // Wait, logic inversion check:
    // ui_wizard_start sets step to CALIBRATION and calls start_calibration.
    // ui_calibration calls ui_wizard_next WHEN DONE.
    // So if we are in CALIBRATION state and next() is called, it means we
    // finished it.
    current_step = WIZARD_STEP_WIFI_ASK;
    create_ask_wifi_screen();
    break;

  case WIZARD_STEP_WIFI_ASK:
    // Handled by button events calling next() with update state
    // If we are here, it means we transition TO the state set by the button
    if (current_step == WIZARD_STEP_WIFI_CONFIG) {
      // Launch Wifi Screen
      // We need a version of wifi screen that calls back wizard instead of
      // dashboard
      ui_create_screen_wifi();
    } else if (current_step == WIZARD_STEP_DONE) {
      ui_wizard_next(); // Recurse to finish
    }
    break;

  case WIZARD_STEP_WIFI_CONFIG:
    // Wi-Fi screen finished
    current_step = WIZARD_STEP_DONE;
    ui_wizard_next();
    break;

  case WIZARD_STEP_DONE:
    save_setup_done();
    if (scr_ask_wifi) {
      lv_obj_del(scr_ask_wifi);
      scr_ask_wifi = NULL;
    }
    ui_init(); // Launch Main App
    break;
  }
}

void ui_wizard_start(void) {
  ESP_LOGI(TAG, "Starting Setup Wizard");
  current_step = WIZARD_STEP_CALIBRATION;
  ui_calibration_start(); // Modified to call ui_wizard_next() on completion
}
