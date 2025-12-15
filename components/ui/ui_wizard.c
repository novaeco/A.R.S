#include "ui_wizard.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"
#include "screens/ui_wifi.h"
#include "ui.h"
#include "ui_calibration.h"
#include "ui_helpers.h"
#include "ui_navigation.h"
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include <stdio.h>

static const char *TAG = "ui_wizard";

static void wizard_start_wifi_step(void);

typedef enum {
  WIZARD_STEP_NONE = 0,
  WIZARD_STEP_CALIBRATION,
  WIZARD_STEP_WIFI,
  WIZARD_STEP_SUCCESS,
  WIZARD_STEP_ERROR,
  WIZARD_STEP_DONE
} wizard_step_t;

static wizard_step_t current_step = WIZARD_STEP_NONE;
static wifi_err_reason_t last_wifi_reason = WIFI_REASON_UNSPECIFIED;

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

static const char *wifi_reason_to_text(wifi_err_reason_t reason) {
  switch (reason) {
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_AUTH_EXPIRE:
    return "Echec d'authentification Wi-Fi";
  case WIFI_REASON_NO_AP_FOUND:
    return "Point d'acc\u00e8s introuvable";
  case WIFI_REASON_ASSOC_FAIL:
  case WIFI_REASON_ASSOC_EXPIRE:
    return "Association Wi-Fi refus\u00e9e";
  default:
    return "Connexion Wi-Fi interrompue";
  }
}

static void wizard_finish(bool mark_setup_done) {
  ESP_LOGI(TAG, "Wizard finish, mark_done=%d", (int)mark_setup_done);
  if (mark_setup_done) {
    save_setup_done();
  }
  ui_wifi_set_result_cb(NULL);
  current_step = WIZARD_STEP_DONE;
  ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
}

static lv_obj_t *wizard_create_result_screen(const char *title, const char *body,
                                             lv_color_t accent,
                                             bool success_icon,
                                             const char *primary_label,
                                             lv_event_cb_t primary_cb,
                                             const char *secondary_label,
                                             lv_event_cb_t secondary_cb) {
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_theme_apply(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *header =
      ui_helper_create_header(scr, title, NULL, NULL /* unused back label */);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *body_cont = lv_obj_create(scr);
  lv_obj_set_size(body_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_align(body_cont, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT + UI_SPACE_MD);
  lv_obj_set_style_bg_opa(body_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body_cont, 0, 0);
  lv_obj_set_style_pad_all(body_cont, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_gap(body_cont, UI_SPACE_LG, 0);
  lv_obj_set_flex_flow(body_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon = lv_label_create(body_cont);
  uint32_t accent_hex = lv_color_to32(accent) & 0xFFFFFF;
  lv_label_set_text_fmt(icon, "#%06x %s#", (unsigned int)accent_hex,
                        success_icon ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
  lv_label_set_recolor(icon, true);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);

  lv_obj_t *lbl_body = lv_label_create(body_cont);
  lv_label_set_text(lbl_body, body);
  lv_label_set_long_mode(lbl_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_body, LV_PCT(80));
  lv_obj_add_style(lbl_body, &ui_style_text_body, 0);
  lv_obj_set_style_text_align(lbl_body, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *actions = lv_obj_create(body_cont);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_pad_gap(actions, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (primary_label && primary_cb) {
    lv_obj_t *btn_primary = lv_button_create(actions);
    lv_obj_add_style(btn_primary, &ui_style_btn_primary, 0);
    lv_obj_set_style_min_width(btn_primary, 180, 0);
    lv_obj_set_style_min_height(btn_primary, 52, 0);
    lv_obj_add_event_cb(btn_primary, primary_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_primary);
    lv_label_set_text(lbl, primary_label);
    lv_obj_center(lbl);
  }

  if (secondary_label && secondary_cb) {
    lv_obj_t *btn_secondary = lv_button_create(actions);
    lv_obj_add_style(btn_secondary, &ui_style_btn_secondary, 0);
    lv_obj_set_style_min_width(btn_secondary, 160, 0);
    lv_obj_set_style_min_height(btn_secondary, 52, 0);
    lv_obj_add_event_cb(btn_secondary, secondary_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_secondary);
    lv_label_set_text(lbl, secondary_label);
    lv_obj_center(lbl);
  }

  return scr;
}

static void wizard_success_continue(lv_event_t *e) { (void)e; wizard_finish(true); }

static void wizard_skip_offline(lv_event_t *e) {
  (void)e;
  ESP_LOGW(TAG, "Wizard skipped after Wi-Fi failure");
  wizard_finish(true);
}

static void wizard_retry_wifi(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Retrying Wi-Fi step from wizard");
  wizard_start_wifi_step();
}

static void wizard_show_success(void) {
  current_step = WIZARD_STEP_SUCCESS;
  lv_obj_t *scr = wizard_create_result_screen(
      "Configuration termin\u00e9e",
      "La calibration est effectu\u00e9e et le Wi-Fi est connect\u00e9. Vous pouvez"
      " utiliser le tableau de bord.",
      lv_palette_main(LV_PALETTE_GREEN), true, "Aller au dashboard",
      wizard_success_continue, NULL, NULL);
  ui_switch_screen(scr, LV_SCR_LOAD_ANIM_OVER_RIGHT);
}

static void wizard_show_error(wifi_err_reason_t reason) {
  current_step = WIZARD_STEP_ERROR;
  const char *reason_text = wifi_reason_to_text(reason);
  char body[160];
  snprintf(body, sizeof(body),
           "La connexion Wi-Fi a \u00e9chou\u00e9 (%s). Vous pouvez r\u00e9essayer ou"
           " passer en mode hors ligne.",
           reason_text);

  lv_obj_t *scr = wizard_create_result_screen("Wi-Fi en \u00e9chec", body,
                                              lv_palette_main(LV_PALETTE_RED),
                                              false, "R\u00e9essayer",
                                              wizard_retry_wifi, "Passer",
                                              wizard_skip_offline);
  ui_switch_screen(scr, LV_SCR_LOAD_ANIM_OVER_LEFT);
}

static void wizard_wifi_result_cb(ui_wifi_result_t result,
                                  wifi_err_reason_t reason) {
  last_wifi_reason = reason;
  ESP_LOGI(TAG, "Wizard received Wi-Fi result=%d reason=%d", (int)result,
           (int)reason);
  ui_wifi_set_result_cb(NULL);

  if (result == UI_WIFI_RESULT_SUCCESS) {
    wizard_show_success();
  } else {
    wizard_show_error(reason);
  }
}

static void wizard_start_wifi_step(void) {
  ESP_LOGI(TAG, "Wizard entering Wi-Fi step");
  current_step = WIZARD_STEP_WIFI;
  ui_wifi_set_result_cb(wizard_wifi_result_cb);
  ui_nav_navigate(UI_SCREEN_WIFI, true);
}

void ui_wizard_next(void) {
  ESP_LOGI(TAG, "Wizard next from step %d", current_step);
  if (current_step == WIZARD_STEP_CALIBRATION) {
    wizard_start_wifi_step();
  }
}

bool ui_wizard_handle_wifi_cancel(void) {
  if (current_step != WIZARD_STEP_WIFI) {
    return false;
  }
  ESP_LOGW(TAG, "Wizard Wi-Fi cancelled by user");
  ui_wifi_set_result_cb(NULL);
  wizard_show_error(last_wifi_reason);
  return true;
}

bool ui_wizard_is_running(void) {
  return current_step != WIZARD_STEP_NONE && current_step != WIZARD_STEP_DONE;
}

void ui_wizard_start(void) {
  ESP_LOGI(TAG, "Starting Setup Wizard");
  current_step = WIZARD_STEP_CALIBRATION;
  last_wifi_reason = WIFI_REASON_UNSPECIFIED;
  ui_calibration_start();
}
#include "ui_wizard.h"
#include "esp_log.h"
#include "lvgl.h"
#include "net_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "screens/ui_wifi.h"
#include "ui.h"
#include "ui_calibration.h"
#include "ui_navigation.h"

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
      ui_nav_navigate(UI_SCREEN_WIFI, true);
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
    ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
    break;
  }
}

void ui_wizard_start(void) {
  ESP_LOGI(TAG, "Starting Setup Wizard");
  current_step = WIZARD_STEP_CALIBRATION;
  ui_calibration_start(); // Modified to call ui_wizard_next() on completion
}
