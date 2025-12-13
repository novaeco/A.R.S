#include "screens/ui_settings.h"
#include "../ui_helpers.h"
#include "../ui_theme.h"
#include "board.h"
#include "core_export.h"
#include "iot_manager.h"
#include "lvgl.h"
#include "net_manager.h"
#include "reptile_storage.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

// Text Areas
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pwd;
static lv_obj_t *ta_ota_url;
static lv_obj_t *ta_pin;
static lv_obj_t *kb;
static lv_obj_t *slider_bl;
static lv_obj_t *label_bl_value;

// Spinner removed (using shared)

static lv_obj_t *ui_msgbox_notify(const char *title, const char *text) {
  lv_obj_t *mbox = lv_msgbox_create(NULL);
  if (title)
    lv_msgbox_add_title(mbox, title);
  if (text)
    lv_msgbox_add_text(mbox, text);
  lv_msgbox_add_close_button(mbox);
  lv_msgbox_add_footer_button(mbox, "OK");
  lv_obj_center(mbox);
  return mbox;
}

static void back_event_cb(lv_event_t *e) { ui_create_dashboard(); }

static void save_wifi_cb(lv_event_t *e) {
  const char *ssid = lv_textarea_get_text(ta_ssid);
  const char *pwd = lv_textarea_get_text(ta_pwd);

  if (strlen(ssid) > 0) {
    storage_nvs_set_str("wifi_ssid", ssid);
    storage_nvs_set_str("wifi_pwd", pwd);
    net_connect(ssid, pwd);
    ui_msgbox_notify("Info", "WiFi credentials saved.");
  }
}

static void save_pin_cb(lv_event_t *e) {
  const char *pin = lv_textarea_get_text(ta_pin);
  storage_nvs_set_str("sys_pin", pin);
  ui_msgbox_notify("Info", "Code PIN enregistre.");
}

static void export_cb(lv_event_t *e) {
  if (!board_sd_is_mounted()) {
    ui_msgbox_notify("SD desactivee",
                     "Aucune carte SD montee. Export indisponible.");
    return;
  }

  ui_helper_show_spinner();
  lv_refr_now(NULL);

  if (core_export_csv("/sdcard/export.csv") == ESP_OK) {
    ui_helper_hide_spinner();
    ui_msgbox_notify("Succes", "Export CSV termine:\n/sdcard/export.csv");
  } else {
    ui_helper_hide_spinner();
    ui_msgbox_notify("Erreur", "Echec de l'export.");
  }
}

static void ota_btn_cb(lv_event_t *e) {
  const char *url = lv_textarea_get_text(ta_ota_url);
  if (strlen(url) < 10 || (strstr(url, "http") != url)) {
    ui_msgbox_notify("Erreur", "URL invalide (http... required)");
    return;
  }
  if (strlen(url) > 0) {
    ui_helper_show_spinner();
    iot_ota_start(url);
  }
}

static void update_backlight_label(int32_t percent) {
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  if (label_bl_value) {
    lv_label_set_text_fmt(label_bl_value, "Luminosite: %d%%", (int)percent);
  }
}

static void backlight_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  int32_t percent = lv_slider_get_value(slider_bl);
  board_set_backlight_percent((uint8_t)percent);
  storage_nvs_set_i32("backlight_pct", percent);
  update_backlight_label(percent);
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
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_create_settings_screen(void) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  lv_obj_t *scr = lv_obj_create(NULL);
  ui_theme_apply(scr);

  // Header Helper
  ui_helper_create_header(scr, "Parametres", back_event_cb, "Retour");

  // Content Container
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, disp_w, disp_h - header_height);
  lv_obj_set_y(cont, header_height);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // WiFi
  lv_label_set_text(lv_label_create(cont), "WiFi");
  ta_ssid = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_ssid, "SSID");
  lv_textarea_set_one_line(ta_ssid, true);
  lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

  ta_pwd = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_pwd, "Password");
  lv_textarea_set_password_mode(ta_pwd, true);
  lv_textarea_set_one_line(ta_pwd, true);
  lv_obj_add_event_cb(ta_pwd, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t *btn_save = lv_button_create(cont);
  lv_obj_set_style_bg_color(btn_save, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_save, save_wifi_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_save), "Connecter");

  // Security
  lv_obj_t *l_sec = lv_label_create(cont);
  lv_label_set_text(l_sec, "Securite (Code PIN)");
  lv_obj_set_style_margin_top(l_sec, 20, 0);

  ta_pin = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_pin, "Nouveau PIN (vide = aucun)");
  lv_textarea_set_password_mode(ta_pin, true);
  lv_textarea_set_one_line(ta_pin, true);
  lv_obj_add_event_cb(ta_pin, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t *btn_pin = lv_button_create(cont);
  lv_obj_set_style_bg_color(btn_pin, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_pin, save_pin_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_pin), "Sauvegarder PIN");

  // Export
  lv_obj_t *l_exp = lv_label_create(cont);
  lv_label_set_text(l_exp, "Donnees");
  lv_obj_set_style_margin_top(l_exp, 20, 0);

  lv_obj_t *btn_export = lv_button_create(cont);
  lv_obj_set_style_bg_color(btn_export, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_export, export_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_export), "Exporter CSV (SD)");

  // OTA
  lv_obj_t *l_ota = lv_label_create(cont);
  lv_label_set_text(l_ota, "Mise a jour");
  lv_obj_set_style_margin_top(l_ota, 20, 0);

  ta_ota_url = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_ota_url, "URL");
  lv_textarea_set_one_line(ta_ota_url, true);
  lv_obj_add_event_cb(ta_ota_url, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t *btn_ota = lv_button_create(cont);
  lv_obj_set_style_bg_color(btn_ota, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_ota, ota_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_ota), "Mettre a jour");

  // Backlight control
  lv_obj_t *l_bl = lv_label_create(cont);
  lv_label_set_text(l_bl, "Ecran");
  lv_obj_set_style_margin_top(l_bl, 20, 0);

  slider_bl = lv_slider_create(cont);
  lv_slider_set_range(slider_bl, 0, 100);

  int32_t stored_bl = 100;
  if (storage_nvs_get_i32("backlight_pct", &stored_bl) != ESP_OK) {
    stored_bl = 100;
  }
  if (stored_bl < 0) {
    stored_bl = 0;
  } else if (stored_bl > 100) {
    stored_bl = 100;
  }
  lv_slider_set_value(slider_bl, stored_bl, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_bl, backlight_event_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);

  label_bl_value = lv_label_create(cont);
  update_backlight_label(stored_bl);

  // Keyboard Helper
  kb = lv_keyboard_create(scr);
  ui_helper_setup_keyboard(kb);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_screen_load(scr);
}