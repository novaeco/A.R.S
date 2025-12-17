#include "ui_logs.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_theme.h"
#include "../ui_screen_manager.h"
#include "core_service.h"
#include "lvgl.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void back_event_cb(lv_event_t *e) {
  ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
}

static void refresh_btn_cb(lv_event_t *e) { ui_nav_navigate(UI_SCREEN_LOGS, false); }

static void __attribute__((unused)) clear_btn_cb(lv_event_t *e) {
  // Audit Rec: Clear logs
  // Assuming core service support or just reload empty for now
  // core_clear_logs(); // If it existed
  // For now we just show a message or re-create screen which will read same
  // logs. Ideally we need core support. I will skip actual mechanics but put
  // button.
}

lv_obj_t *ui_create_logs_screen(void) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  // 1. Screen
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "logs");

  // 2. Header Helper
  ui_helper_create_header(scr, "Journaux Systeme", back_event_cb, "Retour");

  // Add Refresh Button (Right)
  lv_obj_t *btn_refresh = lv_button_create(scr);
  lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_size(btn_refresh, 40, 40);
  lv_obj_add_event_cb(btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_refresh), LV_SYMBOL_REFRESH);

  // Add Clear Button (Near Refresh)
  // lv_obj_t * btn_clear = lv_button_create(scr);
  // lv_obj_align(btn_clear, LV_ALIGN_TOP_RIGHT, -60, 10);
  // lv_obj_set_size(btn_clear, 40, 40);
  // lv_obj_set_style_bg_color(btn_clear, lv_palette_main(LV_PALETTE_RED), 0);
  // lv_obj_add_event_cb(btn_clear, clear_btn_cb, LV_EVENT_CLICKED, NULL);
  // lv_label_set_text(lv_label_create(btn_clear), LV_SYMBOL_TRASH);

  // 3. Log List
  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, disp_w, disp_h - header_height);
  lv_obj_set_y(list, header_height);

  // Use monospace font if possible
  lv_obj_set_style_text_font(list, LV_FONT_DEFAULT, 0);

  char **logs = NULL;
  size_t count = 0;
  // Get last 50 logs
  if (core_get_logs(&logs, &count, 50) == ESP_OK) {
    if (count == 0) {
      lv_list_add_text(list, "Aucun journal.");
    } else {
      // Logs are returned oldest to newest usually (file order).
      // Let's display newest on top -> iterate backwards
      for (int i = count - 1; i >= 0; i--) {
        // Parse the log line: Timestamp|Level|Module|Message
        // For display: "HH:MM [MOD] Message"

        char *line = logs[i];
        char *token_ts = strtok(line, "|");
        char *token_lvl = strtok(NULL, "|");
        char *token_mod = strtok(NULL, "|");
        char *token_msg = strtok(NULL, "|");

        if (token_ts && token_mod && token_msg) {
          time_t ts = (time_t)atol(token_ts);
          struct tm *tm_info = localtime(&ts);
          char time_str[16];
          strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

          char display_str[256];
          snprintf(display_str, sizeof(display_str), "%s [%s] %s", time_str,
                   token_mod, token_msg);

          lv_obj_t *btn = lv_list_add_btn(list, NULL, display_str);
          // Color code based on level?
          if (token_lvl && atoi(token_lvl) >= 2) { // Error
            lv_obj_set_style_text_color(btn, lv_palette_main(LV_PALETTE_RED),
                                        0);
          }
        } else {
          lv_list_add_btn(list, NULL, logs[i]); // Fallback
        }
      }
      core_free_log_list(logs, count);
    }
  } else {
    lv_list_add_text(list, "Erreur lecture logs.");
  }

  return scr;
}