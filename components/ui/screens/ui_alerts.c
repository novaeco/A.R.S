#include "screens/ui_alerts.h"
#include "../ui_navigation.h"
#include "core_service_alerts.h"
#include "esp_err.h"
#include "lvgl.h"
#include "screens/ui_lockscreen.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

static void back_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
  }
}

lv_obj_t *ui_create_alerts_screen(void) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  // 1. Screen
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

  // 2. Header
  lv_obj_t *header = lv_obj_create(scr);
  lv_obj_set_size(header, LV_PCT(100), header_height);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_ORANGE),
                            0); // Orange for alerts

  lv_obj_t *btn_back = lv_button_create(header);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Alertes & Rappels");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // 3. List
  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, disp_w, disp_h - header_height);
  lv_obj_set_y(list, header_height);

  core_alert_t *alerts = NULL;
  size_t count = 0;

  if (core_get_alerts(&alerts, &count) == ESP_OK) {
    if (count == 0) {
      lv_list_add_text(list, "Aucune alerte. Tout va bien !");
    } else {
      for (size_t i = 0; i < count; i++) {
        // On utilise ici le champ 'message' de la structure core_alert_t
        lv_obj_t *btn =
            lv_list_add_btn(list, LV_SYMBOL_WARNING, alerts[i].message);
        lv_obj_set_style_text_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
      }
      core_free_alert_list(alerts, count);
    }
  } else {
    lv_list_add_text(list, "Erreur verification alertes.");
  }

  return scr;
}