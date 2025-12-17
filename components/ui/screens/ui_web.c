#include "ui_web.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_theme.h"
#include "lvgl.h"
#include "net_manager.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *ta_result;

static void search_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {

    if (!net_is_connected()) {
      lv_textarea_set_text(ta_result, "ECHEC: Pas de connexion WiFi (IP).");
      return;
    }

    lv_textarea_set_text(ta_result, "Test en cours (ping httpbin.org)...");

    // Helper Spinner
    ui_helper_show_spinner();
    lv_refr_now(NULL);

    // Simple connectivity check
    const char *url = "http://httpbin.org/get";

    // Buffer for response
    static char response_buf[512];
    esp_err_t err = net_http_get(url, response_buf, sizeof(response_buf));

    ui_helper_hide_spinner();

    if (err == ESP_OK) {
      lv_textarea_set_text(
          ta_result,
          "SUCCES: Connexion Internet OK !\nReponse recue du serveur.");
    } else {
      lv_textarea_set_text(ta_result,
                           "ECHEC: Erreur requete HTTP.\nVerifier Internet.");
    }
  }
}

static void back_cb_wrapper(lv_event_t *e) {
  ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
}

lv_obj_t *ui_create_web_screen(void) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  // 1. Screen
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "web");
  // lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0); // Theme
  // standard

  // 2. Header Helper
  ui_helper_create_header(scr, "Requêtes Web (Test)", back_cb_wrapper,
                          "Retour");

  // 3. Content
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, disp_w, disp_h - header_height);
  lv_obj_set_y(cont, header_height);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cont, 20, 0);
  lv_obj_set_style_pad_gap(cont, 15, 0);

  // Labels & Fields
  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Test de Connectivite Internet:");

  // Status Area
  ta_result = lv_textarea_create(cont);
  lv_obj_set_size(ta_result, LV_PCT(100), 80);
  lv_obj_set_style_text_font(ta_result, LV_FONT_DEFAULT, 0);
  lv_textarea_set_text(ta_result, "Appuyez sur 'Tester' pour verifier...");

  // Test Button
  lv_obj_t *btn_test = lv_button_create(cont);
  lv_obj_set_width(btn_test, LV_PCT(60));
  lv_obj_set_style_bg_color(btn_test, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_set_style_bg_color(btn_test, lv_palette_darken(LV_PALETTE_GREEN, 2),
                            LV_STATE_PRESSED);
  lv_obj_align(btn_test, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn_test, search_event_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn_test);
  lv_label_set_text(lbl, "TESTER CONNEXION");
  lv_obj_center(lbl);

  // OTA Section
  lv_obj_t *line = lv_line_create(cont);
  static lv_point_precise_t line_points[] = {{0, 0}, {800, 0}};
  lv_line_set_points(line, line_points, 2);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_color(line, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_line_opa(line, LV_OPA_50, 0);

  lbl = lv_label_create(cont);
  lv_label_set_text(lbl, "Mise a jour Firmware (OTA):");

  lv_obj_t *ta_ota_url = lv_textarea_create(cont);
  lv_textarea_set_one_line(ta_ota_url, true);
  lv_obj_set_width(ta_ota_url, LV_PCT(100));
  lv_textarea_set_placeholder_text(ta_ota_url, "http://.../firmware.bin");

  lv_obj_t *btn_ota = lv_button_create(cont);
  lv_obj_set_width(btn_ota, LV_PCT(60));
  lv_obj_set_style_bg_color(btn_ota, lv_palette_main(LV_PALETTE_PURPLE), 0);
  lv_obj_set_style_bg_color(btn_ota, lv_palette_darken(LV_PALETTE_PURPLE, 2),
                            LV_STATE_PRESSED);
  lv_obj_align(btn_ota, LV_ALIGN_CENTER, 0, 0);

  // Reusing search cb for simulation
  lv_obj_add_event_cb(btn_ota, search_event_cb, LV_EVENT_CLICKED, NULL);

  lbl = lv_label_create(btn_ota);
  lv_label_set_text(lbl, "LANCER MISE A JOUR");
  lv_obj_center(lbl);

  return scr;
}