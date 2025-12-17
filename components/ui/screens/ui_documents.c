#include "ui_documents.h"
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

static char *ui_strdup(const char *src) {
  if (!src)
    return NULL;
  size_t len = strlen(src) + 1;
  char *dst = malloc(len);
  if (dst) {
    memcpy(dst, src, len);
  }
  return dst;
}

// Spinner removed (using shared)

static void back_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
  }
}

static void animal_select_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    const char *animal_id = (const char *)lv_event_get_user_data(e);
    if (animal_id) {
      // Helper Spinner
      ui_helper_show_spinner();
      lv_refr_now(NULL);

      if (core_generate_report(animal_id) == ESP_OK) {
        LV_LOG_USER("Report generated for %s", animal_id);
      } else {
        LV_LOG_ERROR("Failed to generate report");
      }

      ui_helper_hide_spinner();
      // Reload documents screen
      ui_nav_navigate(UI_SCREEN_DOCUMENTS, false);
    }
  }
}

static lv_obj_t *modal_cont;
static void close_modal_cb(lv_event_t *e) {
  if (modal_cont) {
    lv_obj_del(modal_cont);
    modal_cont = NULL;
  }
}

static void generate_report_btn_cb(lv_event_t *e) {
  // Show animal selection list (Modal)
  modal_cont = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal_cont, LV_PCT(80), LV_PCT(80));
  lv_obj_center(modal_cont);
  lv_obj_set_style_bg_color(modal_cont, lv_palette_darken(LV_PALETTE_GREY, 3),
                            0);

  lv_obj_t *title = lv_label_create(modal_cont);
  lv_label_set_text(title, "Selectionner un animal :");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *close_btn = lv_button_create(modal_cont);
  lv_obj_set_size(close_btn, 40, 40);
  lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(close_btn, close_modal_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(close_btn), LV_SYMBOL_CLOSE);

  lv_obj_t *list = lv_list_create(modal_cont);
  lv_obj_set_size(list, LV_PCT(100), LV_PCT(80));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Load animals
  animal_summary_t *animals = NULL;
  size_t count = 0;
  if (core_list_animals(&animals, &count) == ESP_OK) {
    for (size_t i = 0; i < count; i++) {
      char label_txt[256];
      snprintf(label_txt, sizeof(label_txt), "%s (%s)", animals[i].name,
               animals[i].species);
      lv_obj_t *btn = lv_list_add_btn(list, NULL, label_txt);

      // Store ID copies for callback
      char *id_copy = ui_strdup(animals[i].id);
      lv_obj_add_event_cb(btn, animal_select_event_cb, LV_EVENT_CLICKED,
                          id_copy);
    }
    core_free_animal_list(animals);
  }
}

// Helper to delete list item
static void __attribute__((unused)) delete_report_event_cb(lv_event_t *e) {
  // Logic to delete file could be added here
  // For now purely UI refresh
  ui_nav_navigate(UI_SCREEN_DOCUMENTS, false);
}

lv_obj_t *ui_create_documents_screen(void) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "documents");

  // Header Helper
  ui_helper_create_header(scr, "Documents (PDF)", back_event_cb, "Retour");

  lv_obj_t *btn_gen = lv_button_create(
      lv_obj_get_child(scr, 0)); // Hack to get header or just create new
  // Better to use absolute positioning or create separate button
  // Re-creating button in header area manually for "Generate"
  btn_gen = lv_button_create(scr);
  lv_obj_align(btn_gen, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_size(btn_gen, 140, 40);
  lv_obj_add_event_cb(btn_gen, generate_report_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_bg_color(btn_gen, lv_palette_darken(LV_PALETTE_GREEN, 2), 0);
  lv_obj_t *l = lv_label_create(btn_gen);
  lv_label_set_text(l, LV_SYMBOL_PLUS " Nouveau");
  lv_obj_center(l);

  // List
  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, disp_w, disp_h - header_height);
  lv_obj_set_y(list, header_height);

  // Load existing reports
  char **reports = NULL;
  size_t count = 0;
  if (core_list_reports(&reports, &count) == ESP_OK) {
    if (count == 0) {
      lv_list_add_text(list, "Aucun rapport disponible.");
    } else {
      for (size_t i = 0; i < count; i++) {
        lv_list_add_btn(list, LV_SYMBOL_FILE, reports[i]);
        // Add delete option?
        // Simple list for now
      }
      // Free list
      // Implementation specific based on core_list_reports alloc method
      // Assuming we need to free array + strings
      for (size_t i = 0; i < count; i++)
        free(reports[i]);
      free(reports);
    }
  }

  return scr;
}