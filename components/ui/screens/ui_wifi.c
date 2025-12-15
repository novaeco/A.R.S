#include "screens/ui_wifi.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_theme.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "net_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI_WIFI";

lv_obj_t *ui_ScreenWifi;

// Text Areas
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;
static lv_obj_t *kb;
static lv_obj_t *btn_connect;
static lv_obj_t *lbl_status;

static esp_event_handler_instance_t s_wifi_evt_handler;
static esp_event_handler_instance_t s_ip_evt_handler;
static bool s_wifi_evt_registered = false;

typedef struct {
  bool got_ip;
  bool disconnected;
  wifi_err_reason_t reason;
  char ip_str[16];
} wifi_ui_update_t;

static void set_inputs_enabled(bool enabled) {
  if (enabled) {
    lv_obj_clear_state(ta_ssid, LV_STATE_DISABLED);
    lv_obj_clear_state(ta_pass, LV_STATE_DISABLED);
    lv_obj_clear_state(btn_connect, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(ta_ssid, LV_STATE_DISABLED);
    lv_obj_add_state(ta_pass, LV_STATE_DISABLED);
    lv_obj_add_state(btn_connect, LV_STATE_DISABLED);
    if (kb) {
      lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void set_status_label(const char *text, lv_color_t color) {
  if (!lbl_status) {
    return;
  }
  lv_label_set_text(lbl_status, text);
  lv_obj_set_style_text_color(lbl_status, color, 0);
}

static void apply_wifi_ui_update(void *user_data) {
  wifi_ui_update_t *update = (wifi_ui_update_t *)user_data;
  if (!update) {
    return;
  }

  ui_helper_hide_spinner();

  if (update->got_ip) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Connect\u00e9: %s", update->ip_str);
    set_status_label(buf, lv_palette_main(LV_PALETTE_GREEN));
    set_inputs_enabled(true);
  } else if (update->disconnected) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "Echec Wi-Fi (reason=%d). V\u00e9rifiez SSID/MdP.",
             (int)update->reason);
    set_status_label(buf, lv_palette_main(LV_PALETTE_RED));
    set_inputs_enabled(true);
  }

  free(update);
}

static void wifi_ui_event_forwarder(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data) {
  wifi_ui_update_t *update = calloc(1, sizeof(wifi_ui_update_t));
  if (!update) {
    ESP_LOGE(TAG, "Allocation failed for Wi-Fi UI update");
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    update->got_ip = true;
    update->disconnected = false;
    snprintf(update->ip_str, sizeof(update->ip_str), IPSTR,
             IP2STR(&event->ip_info.ip));
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disc =
        (wifi_event_sta_disconnected_t *)event_data;
    update->disconnected = true;
    update->got_ip = false;
    update->reason = disc ? disc->reason : WIFI_REASON_UNSPECIFIED;
  } else {
    free(update);
    return;
  }

  if (lv_async_call(apply_wifi_ui_update, update) != LV_RESULT_OK) {
    ESP_LOGW(TAG, "Failed to enqueue Wi-Fi UI update");
    free(update);
  }
}

static esp_err_t ensure_wifi_event_listener(void) {
  if (s_wifi_evt_registered) {
    return ESP_OK;
  }

  esp_err_t err = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ui_event_forwarder, NULL,
      &s_wifi_evt_handler);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Wi-Fi event handler register failed: %s",
             esp_err_to_name(err));
    return err;
  }

  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_ui_event_forwarder, NULL,
                                            &s_ip_evt_handler);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IP event handler register failed: %s", esp_err_to_name(err));
    return err;
  }

  s_wifi_evt_registered = true;
  return ESP_OK;
}

static void event_connect_handler(lv_event_t *e) {
  const char *ssid = lv_textarea_get_text(ta_ssid);
  const char *pass = lv_textarea_get_text(ta_pass);

  size_t ssid_len = strlen(ssid);
  size_t pass_len = strlen(pass);

  if (ssid_len == 0) {
    ESP_LOGW(TAG, "SSID is empty");
    set_status_label("SSID requis", lv_palette_main(LV_PALETTE_RED));
    return;
  }

  if (ssid_len > sizeof(((wifi_config_t *)0)->sta.ssid) - 1) {
    ESP_LOGW(TAG, "SSID too long");
    set_status_label("SSID trop long", lv_palette_main(LV_PALETTE_RED));
    return;
  }

  for (size_t i = 0; i < ssid_len; ++i) {
    if ((unsigned char)ssid[i] < 0x20 || (unsigned char)ssid[i] > 0x7E) {
      ESP_LOGW(TAG, "SSID contains unsupported characters");
      set_status_label("Caract\u00e8res invalides dans le SSID",
                      lv_palette_main(LV_PALETTE_RED));
      return;
    }
  }

  if (pass_len < 8 || pass_len > 63) {
    ESP_LOGW(TAG, "Password length invalid (%d)", (int)pass_len);
    set_status_label("Mot de passe 8-63 caract\u00e8res", lv_palette_main(LV_PALETTE_RED));
    return;
  }

  for (size_t i = 0; i < pass_len; ++i) {
    if ((unsigned char)pass[i] < 0x20 || (unsigned char)pass[i] > 0x7E) {
      ESP_LOGW(TAG, "Password contains unsupported characters");
      set_status_label("Caract\u00e8res invalides dans le mot de passe",
                      lv_palette_main(LV_PALETTE_RED));
      return;
    }
  }

  ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
  // Call Net Manager to start Wi-Fi
  esp_err_t err = net_connect(ssid, pass);
  if (err != ESP_OK) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Connexion refus\u00e9e (%s)", esp_err_to_name(err));
    set_status_label(buf, lv_palette_main(LV_PALETTE_RED));
    return;
  }

  // Helper Spinner
  ui_helper_show_spinner();

  set_status_label("Connexion en cours...", lv_palette_main(LV_PALETTE_BLUE));
  set_inputs_enabled(false);
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
    ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
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
  btn_connect = lv_button_create(ui_ScreenWifi);
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

  // Status Label
  lbl_status = lv_label_create(ui_ScreenWifi);
  lv_label_set_text(lbl_status, "Pr\u00eat");
  lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_LEFT, 20, -20);

  lv_screen_load(ui_ScreenWifi);

  esp_err_t evt_err = ensure_wifi_event_listener();
  if (evt_err != ESP_OK) {
    set_status_label("Moniteur Wi-Fi indisponible", lv_palette_main(LV_PALETTE_RED));
  }
}
