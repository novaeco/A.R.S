#include "screens/ui_wifi.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_screen_manager.h"
#include "../ui_theme.h"
#include "../ui_wizard.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "net_manager.h"
#include "ui.h"
#include <stdbool.h>
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
static lv_obj_t *btn_forget;
static lv_obj_t *spn_connect;
static lv_obj_t *lbl_status;
static ui_wifi_result_cb_t s_result_cb = NULL;
static bool s_waiting_for_connection = false;

static esp_event_handler_instance_t s_wifi_evt_handler;
static esp_event_handler_instance_t s_ip_evt_handler;
static esp_event_handler_instance_t s_net_state_handler;
static bool s_wifi_evt_registered = false;

typedef struct {
  bool got_ip;
  bool disconnected;
  wifi_err_reason_t reason;
  char ip_str[16];
  bool has_state;
  wifi_prov_state_t state;
} wifi_ui_update_t;

static void set_inputs_enabled(bool enabled) {
  if (enabled) {
    lv_obj_clear_state(ta_ssid, LV_STATE_DISABLED);
    lv_obj_clear_state(ta_pass, LV_STATE_DISABLED);
    lv_obj_clear_state(btn_connect, LV_STATE_DISABLED);
    if (btn_forget) {
      lv_obj_clear_state(btn_forget, LV_STATE_DISABLED);
    }
  } else {
    lv_obj_add_state(ta_ssid, LV_STATE_DISABLED);
    lv_obj_add_state(ta_pass, LV_STATE_DISABLED);
    lv_obj_add_state(btn_connect, LV_STATE_DISABLED);
    if (btn_forget) {
      lv_obj_add_state(btn_forget, LV_STATE_DISABLED);
    }
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

static void set_connecting_spinner(bool visible) {
  if (!spn_connect) {
    return;
  }
  if (visible) {
    lv_obj_clear_flag(spn_connect, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(spn_connect, LV_OBJ_FLAG_HIDDEN);
  }
}

static const char *reason_to_text(wifi_err_reason_t reason) {
  switch (reason) {
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_AUTH_EXPIRE:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    return "Authentification refusée (mot de passe?)";
  case WIFI_REASON_NO_AP_FOUND:
    return "Point d'accès introuvable";
  default:
    return "Echec de connexion";
  }
}

static void apply_wifi_ui_update(void *user_data) {
  wifi_ui_update_t *update = (wifi_ui_update_t *)user_data;
  if (!update) {
    return;
  }

  ui_show_loading(false);

  if (update->has_state) {
    switch (update->state) {
    case WIFI_PROV_STATE_NOT_PROVISIONED:
      set_status_label("Non provisionné", lv_palette_main(LV_PALETTE_GREY));
      set_inputs_enabled(true);
      set_connecting_spinner(false);
      break;
    case WIFI_PROV_STATE_CONNECTING:
      set_status_label("Connexion en cours...",
                       lv_palette_main(LV_PALETTE_BLUE));
      set_inputs_enabled(false);
      set_connecting_spinner(true);
      break;
    case WIFI_PROV_STATE_WRONG_PASSWORD:
      set_status_label("Mot de passe incorrect",
                       lv_palette_main(LV_PALETTE_RED));
      set_inputs_enabled(true);
      set_connecting_spinner(false);
      break;
    case WIFI_PROV_STATE_FAILED:
    case WIFI_PROV_STATE_CAPTIVE:
      set_status_label(reason_to_text(update->reason),
                       lv_palette_main(LV_PALETTE_RED));
      set_inputs_enabled(true);
      set_connecting_spinner(false);
      break;
    case WIFI_PROV_STATE_CONNECTED:
      set_connecting_spinner(false);
      break;
    }
  }

  if (update->got_ip) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Connect\u00e9: %s", update->ip_str);
    set_status_label(buf, lv_palette_main(LV_PALETTE_GREEN));
    set_inputs_enabled(true);
    set_connecting_spinner(false);
    bool notify = s_waiting_for_connection;
    s_waiting_for_connection = false;
    if (notify && s_result_cb) {
      s_result_cb(UI_WIFI_RESULT_SUCCESS, WIFI_REASON_UNSPECIFIED);
    }
  } else if (update->disconnected) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "Echec Wi-Fi (reason=%d). V\u00e9rifiez SSID/MdP.",
             (int)update->reason);
    set_status_label(buf, lv_palette_main(LV_PALETTE_RED));
    set_inputs_enabled(true);
    set_connecting_spinner(false);
    ui_show_error(buf);
    if (s_waiting_for_connection && s_result_cb) {
      s_result_cb(UI_WIFI_RESULT_FAILED, update->reason);
    }
    s_waiting_for_connection = false;
  }

  free(update);
}

static void wifi_ui_event_forwarder(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data) {
  (void)arg;
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
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
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

static void net_state_event_forwarder(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base != NET_MANAGER_EVENT ||
      event_id != NET_MANAGER_EVENT_STATE_CHANGED || !event_data) {
    return;
  }

  const net_manager_state_evt_t *evt =
      (const net_manager_state_evt_t *)event_data;
  wifi_ui_update_t *update = calloc(1, sizeof(wifi_ui_update_t));
  if (!update) {
    ESP_LOGE(TAG, "Allocation failed for Wi-Fi state update");
    return;
  }

  update->has_state = true;
  update->state = evt->state;
  update->reason = evt->reason;

  if (lv_async_call(apply_wifi_ui_update, update) != LV_RESULT_OK) {
    ESP_LOGW(TAG, "Failed to enqueue Wi-Fi state update");
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

  err = esp_event_handler_instance_register(
      NET_MANAGER_EVENT, NET_MANAGER_EVENT_STATE_CHANGED, net_state_event_forwarder,
      NULL, &s_net_state_handler);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NET state handler register failed: %s",
             esp_err_to_name(err));
    return err;
  }

  s_wifi_evt_registered = true;
  return ESP_OK;
}

static void unregister_wifi_event_listener(void) {
  if (!s_wifi_evt_registered) {
    return;
  }

  esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        s_wifi_evt_handler);
  esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        s_ip_evt_handler);
  esp_event_handler_instance_unregister(NET_MANAGER_EVENT,
                                        NET_MANAGER_EVENT_STATE_CHANGED,
                                        s_net_state_handler);
  s_wifi_evt_registered = false;
}

static void event_connect_handler(lv_event_t *e) {
  (void)e;
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
    set_status_label("Mot de passe 8-63 caract\u00e8res",
                     lv_palette_main(LV_PALETTE_RED));
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

  esp_err_t ensure_evt = ensure_wifi_event_listener();
  if (ensure_evt != ESP_OK) {
    ui_show_error("Moniteur Wi-Fi indisponible");
    return;
  }

  ESP_LOGI(TAG, "Provisioning SSID: %s", ssid);
  // Persist credentials so provisioning survives reboot and matches UX hints.
  esp_err_t err = net_manager_set_credentials(ssid, pass, true);
  if (err != ESP_OK) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Connexion refus\u00e9e (%s)", esp_err_to_name(err));
    set_status_label(buf, lv_palette_main(LV_PALETTE_RED));
    ui_show_error(buf);
    ui_show_loading(false);
    return;
  }

  ui_show_loading(true);
  set_status_label("Connexion en cours...", lv_palette_main(LV_PALETTE_BLUE));
  set_inputs_enabled(false);
  set_connecting_spinner(true);
  s_waiting_for_connection = true;
}

static void event_forget_handler(lv_event_t *e) {
  (void)e;
  esp_err_t err = net_manager_forget_credentials();
  if (err != ESP_OK) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Impossible d'oublier le r\u00e9seau (%s)",
             esp_err_to_name(err));
    ui_show_error(buf);
    set_status_label(buf, lv_palette_main(LV_PALETTE_RED));
    return;
  }

  lv_textarea_set_text(ta_ssid, "");
  lv_textarea_set_text(ta_pass, "");
  set_inputs_enabled(true);
  set_connecting_spinner(false);
  set_status_label("Identifiants effac\u00e9s",
                   lv_palette_main(LV_PALETTE_GREY));
  s_waiting_for_connection = false;
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

static void back_event_cb(lv_event_t *e) {
  (void)e;
  if (ui_wizard_handle_wifi_cancel()) {
    return;
  }
  ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
}

lv_obj_t *ui_create_screen_wifi(void) {
  ui_ScreenWifi = lv_obj_create(NULL);
  ui_screen_claim_with_theme(ui_ScreenWifi, "wifi");
  lv_obj_clear_flag(ui_ScreenWifi, LV_OBJ_FLAG_SCROLLABLE);

  // Header Helper
  lv_obj_t *header =
      ui_helper_create_header(ui_ScreenWifi, "Wi-Fi Settings", back_event_cb,
                              "Retour");
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

  // Body container with two responsive columns (form + keyboard/status)
  lv_obj_t *body = lv_obj_create(ui_ScreenWifi);
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT + UI_SPACE_MD);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_left(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_right(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_top(body, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_bottom(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_gap(body, UI_SPACE_XL, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *form_col = lv_obj_create(body);
  lv_obj_set_width(form_col, LV_PCT(55));
  lv_obj_set_style_min_width(form_col, 320, 0);
  lv_obj_set_style_flex_grow(form_col, 1, 0);
  lv_obj_set_style_bg_opa(form_col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(form_col, 0, 0);
  lv_obj_set_style_pad_all(form_col, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_gap(form_col, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(form_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(form_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *lbl_hint = lv_label_create(form_col);
  lv_label_set_text(lbl_hint, "Renseignez le SSID et le mot de passe puis "
                              "appuyez sur 'Connect'.\nL'appareil reste en "
                              "attente proprement tant qu'aucun identifiant n"
                              "'est fourni.");
  lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_hint, LV_PCT(100));
  lv_obj_add_style(lbl_hint, &ui_style_text_muted, 0);

  // SSID Label & TextArea
  lv_obj_t *lbl_ssid = lv_label_create(form_col);
  lv_label_set_text(lbl_ssid, "SSID:");
  lv_obj_add_style(lbl_ssid, &ui_style_text_body, 0);

  ta_ssid = lv_textarea_create(form_col);
  lv_textarea_set_one_line(ta_ssid, true);
  lv_obj_set_width(ta_ssid, LV_PCT(100));
  lv_obj_set_height(ta_ssid, 56);
  lv_obj_set_style_pad_all(ta_ssid, UI_SPACE_SM, 0);
  lv_obj_add_style(ta_ssid, &ui_style_text_body, 0);
  lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

  // Password Label & TextArea
  lv_obj_t *lbl_pass = lv_label_create(form_col);
  lv_label_set_text(lbl_pass, "Password:");
  lv_obj_add_style(lbl_pass, &ui_style_text_body, 0);

  ta_pass = lv_textarea_create(form_col);
  lv_textarea_set_one_line(ta_pass, true);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_obj_set_width(ta_pass, LV_PCT(100));
  lv_obj_set_height(ta_pass, 56);
  lv_obj_set_style_pad_all(ta_pass, UI_SPACE_SM, 0);
  lv_obj_add_style(ta_pass, &ui_style_text_body, 0);
  lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

  // Connect Button
  btn_connect = lv_button_create(form_col);
  lv_obj_add_style(btn_connect, &ui_style_btn_primary, 0);
  lv_obj_set_style_min_height(btn_connect, 56, 0);
  lv_obj_set_style_min_width(btn_connect, 200, 0);
  lv_obj_set_style_pad_left(btn_connect, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_right(btn_connect, UI_SPACE_LG, 0);
  lv_obj_add_event_cb(btn_connect, event_connect_handler, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *lbl_btn = lv_label_create(btn_connect);
  lv_label_set_text(lbl_btn, "Connect");
  lv_obj_add_style(lbl_btn, &ui_style_text_body, 0);
  lv_obj_center(lbl_btn);

  btn_forget = lv_button_create(form_col);
  lv_obj_add_style(btn_forget, &ui_style_btn_secondary, 0);
  lv_obj_set_style_min_height(btn_forget, 48, 0);
  lv_obj_set_style_pad_left(btn_forget, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_right(btn_forget, UI_SPACE_LG, 0);
  lv_obj_add_event_cb(btn_forget, event_forget_handler, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *lbl_forget = lv_label_create(btn_forget);
  lv_label_set_text(lbl_forget, "Oublier r\u00e9seau");
  lv_obj_add_style(lbl_forget, &ui_style_text_body, 0);
  lv_obj_center(lbl_forget);

  // Secondary column: status + keyboard
  lv_obj_t *side_col = lv_obj_create(body);
  lv_obj_set_width(side_col, LV_PCT(40));
  lv_obj_set_style_min_width(side_col, 280, 0);
  lv_obj_set_style_flex_grow(side_col, 1, 0);
  lv_obj_set_style_bg_opa(side_col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(side_col, 0, 0);
  lv_obj_set_style_pad_all(side_col, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_gap(side_col, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(side_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(side_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  // Status Label
  lbl_status = lv_label_create(side_col);
  lv_label_set_text(lbl_status, "Pr\u00eat");
  lv_obj_add_style(lbl_status, &ui_style_text_body, 0);
  lv_obj_set_width(lbl_status, LV_PCT(100));

  spn_connect = lv_spinner_create(side_col);
  lv_spinner_set_anim_params(spn_connect, 1000, 60);
  lv_obj_set_size(spn_connect, 48, 48);
  lv_obj_add_flag(spn_connect, LV_OBJ_FLAG_HIDDEN);
  lv_obj_center(spn_connect);

  // --- AZERTY Keyboard Helper ---
  kb = lv_keyboard_create(side_col);
  ui_helper_setup_keyboard(kb);
  lv_obj_set_width(kb, LV_PCT(100));
  lv_obj_set_height(kb, 260);

  // Hide initially
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, NULL);

  return ui_ScreenWifi;
}

void ui_wifi_set_result_cb(ui_wifi_result_cb_t cb) {
  s_result_cb = cb;
  s_waiting_for_connection = false;
}

void ui_wifi_on_enter(void) {
  esp_err_t evt_err = ensure_wifi_event_listener();
  if (evt_err != ESP_OK) {
    set_status_label("Moniteur Wi-Fi indisponible",
                     lv_palette_main(LV_PALETTE_RED));
    ui_show_error("Moniteur Wi-Fi indisponible");
  }
  s_waiting_for_connection = false;
  ui_show_loading(false);
  wifi_ui_update_t *update = calloc(1, sizeof(wifi_ui_update_t));
  if (update) {
    update->has_state = true;
    update->state = net_manager_get_prov_state();
    update->reason = net_manager_get_last_reason();
    net_status_t status = net_get_status();
    if (status.got_ip) {
      update->got_ip = true;
      snprintf(update->ip_str, sizeof(update->ip_str), "%s", status.ip_addr);
    }
    apply_wifi_ui_update(update);
  }
}

void ui_wifi_on_leave(void) {
  ui_show_loading(false);
  set_inputs_enabled(true);
  set_connecting_spinner(false);
  if (kb) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  s_waiting_for_connection = false;
  ui_wifi_set_result_cb(NULL);
  unregister_wifi_event_listener();
}
