#include "screens/ui_wifi.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_theme.h"
#include "esp_log.h"
#include "net_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "UI_WIFI";

lv_obj_t *ui_ScreenWifi;

// Text Areas
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;
static lv_obj_t *kb;

static void event_connect_handler(lv_event_t *e) {
  const char *ssid = lv_textarea_get_text(ta_ssid);
  const char *pass = lv_textarea_get_text(ta_pass);

  if (strlen(ssid) == 0) {
    ESP_LOGW(TAG, "SSID is empty");
    return;
  }

  if (strlen(pass) == 0) {
    ESP_LOGW(TAG, "Password is empty, provisioning will wait for credentials");
    return;
  }

  ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
  // Call Net Manager to start Wi-Fi
  net_connect(ssid, pass);

  // Helper Spinner
  ui_helper_show_spinner();

  // Clean up spinner after timeout
  lv_timer_create((lv_timer_cb_t)ui_helper_hide_spinner, 5000, NULL);
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    if (kb != NULL) {
      lv_keyboard_set_textarea(kb, ta);
      lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (code == LV_EVENT_READY) {
    if (kb != NULL) {
      lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void keyboard_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

#include "../ui_wizard.h"

static void back_event_cb(lv_event_t *e) {
  // If we are in wizard mode (checked via NVS or simple heuristic), we should
  // go next. For now, let's assume if this screen is called during wizard, the
  // user wants to go forward or back. Simpler: Just call ui_wizard_next(). The
  // wizard state machine will handle "WIFI_CONFIG -> DONE" transition. But if
  // we are in dashboard mode, we don't want to call wizard next. Let's rely on
  // the Wizard module to manage what happens next, but here we just need to
  // exit. Ideally, ui_wifi should emit a signal. Let's just call
  // ui_wizard_next() which acts as "Finish/Skip" for this step. BUT! If called
  // from Dashboard, ui_wizard_next might trigger weird state. Safe bet: Check
  // if setup is done.

  nvs_handle_t h;
  uint8_t done = 0;
  if (nvs_open("system", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "setup_done", &done);
    nvs_close(h);
  }

  if (!done) {
    ui_wizard_next();
  } else {
    ui_create_dashboard();
  }
}

void ui_create_screen_wifi(void) {
  ui_ScreenWifi = lv_obj_create(NULL);
  ui_theme_apply(ui_ScreenWifi);
  lv_obj_clear_flag(ui_ScreenWifi, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_hint = lv_label_create(ui_ScreenWifi);
  lv_label_set_text(lbl_hint, "Renseignez le SSID et le mot de passe puis "
                              "appuyez sur 'Connect'.\nL'appareil reste en "
                              "attente proprement tant qu'aucun identifiant n"
                              "'est fourni.");
  lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_hint, 420);
  lv_obj_align(lbl_hint, LV_ALIGN_TOP_LEFT, 20, 20);

  // Header Helper
  ui_helper_create_header(ui_ScreenWifi, "Wi-Fi Settings", back_event_cb,
                          "Retour");

  // SSID Label & TextArea
  lv_obj_t *lbl_ssid = lv_label_create(ui_ScreenWifi);
  lv_label_set_text(lbl_ssid, "SSID:");
  lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 20, 70);

  ta_ssid = lv_textarea_create(ui_ScreenWifi);
  lv_textarea_set_one_line(ta_ssid, true);
  lv_obj_set_width(ta_ssid, 200);
  lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 20, 95);
  lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

  // Password Label & TextArea
  lv_obj_t *lbl_pass = lv_label_create(ui_ScreenWifi);
  lv_label_set_text(lbl_pass, "Password:");
  lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 20, 140);

  ta_pass = lv_textarea_create(ui_ScreenWifi);
  lv_textarea_set_one_line(ta_pass, true);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_set_width(ta_pass, 200);
  lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 20, 165);
  lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

  // Connect Button
  lv_obj_t *btn_connect = lv_button_create(ui_ScreenWifi);
  lv_obj_align(btn_connect, LV_ALIGN_TOP_RIGHT, -20, 95);
  lv_obj_set_size(btn_connect, 100, 50);
  lv_obj_set_style_bg_color(btn_connect, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_connect, event_connect_handler, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *lbl_btn = lv_label_create(btn_connect);
  lv_label_set_text(lbl_btn, "Connect");
  lv_obj_center(lbl_btn);

  // --- AZERTY Keyboard Helper ---
  kb = lv_keyboard_create(ui_ScreenWifi);
  ui_helper_setup_keyboard(kb);

  // Hide initially
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, NULL);

  lv_screen_load(ui_ScreenWifi);
}
