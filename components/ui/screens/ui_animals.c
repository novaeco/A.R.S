#include "screens/ui_animals.h"
#include "../ui_helpers.h"
#include "../ui_theme.h" // Assuming theme header
#include "../ui_navigation.h"
#include "../ui_screen_manager.h"
#include "core_service.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_animal_details.h"
#include "ui_animal_form.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI_ANIMALS";

static lv_obj_t *ta_search;
static lv_obj_t *list_animals;
static lv_obj_t *kb; // Add keyboard reference for AZERTY setup

typedef struct {
  char *id_copy;
} animal_btn_ctx_t;

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

static void animal_item_wrapper_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  animal_btn_ctx_t *ctx = (animal_btn_ctx_t *)lv_event_get_user_data(e);

  if (!ctx)
    return;

  if (code == LV_EVENT_CLICKED) {
    if (ctx->id_copy)
      ui_nav_navigate_ctx(UI_SCREEN_ANIMAL_DETAILS, ctx->id_copy, true);
  } else if (code == LV_EVENT_DELETE) {
    if (ctx->id_copy) {
      ESP_LOGD(TAG, "Free animal list user_data for btn %s", ctx->id_copy);
      free(ctx->id_copy);
      ctx->id_copy = NULL;
    }
    free(ctx);
  }
}

static void clear_animal_list_items(void) {
  if (!list_animals)
    return;

  while (lv_obj_get_child_count(list_animals) > 0) {
    lv_obj_t *child = lv_obj_get_child(list_animals, 0);
    lv_obj_del(child);
  }
}

static void load_animal_list_correct(const char *query) {
  clear_animal_list_items();

  animal_summary_t *animals = NULL;
  size_t count = 0;
  if (core_search_animals(query, &animals, &count) == ESP_OK) {
    if (count == 0) {
      if (query && strlen(query) > 0) {
        lv_list_add_text(list_animals, "Aucun resultat pour la recherche.");
      } else {
        // Empty state visual
        lv_obj_t *cont_empty = lv_obj_create(list_animals);
        lv_obj_set_size(cont_empty, LV_PCT(100), 100);
        lv_obj_set_style_bg_opa(cont_empty, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont_empty, 0, 0);
        lv_obj_t *l = lv_label_create(cont_empty);
        lv_label_set_text(l, LV_SYMBOL_DIRECTORY
                          "\nListe vide. Ajoutez un animal !");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
      }
    } else {
      for (size_t i = 0; i < count; i++) {
        animal_btn_ctx_t *ctx = calloc(1, sizeof(animal_btn_ctx_t));
        if (!ctx)
          continue;

        ctx->id_copy = ui_strdup(animals[i].id);
        if (!ctx->id_copy) {
          free(ctx);
          continue;
        }
        char label[256];
        snprintf(label, sizeof(label), "%s (%s)", animals[i].name,
                 animals[i].species);

        lv_obj_t *btn = lv_list_add_btn(list_animals, LV_SYMBOL_PASTE, label);
        lv_obj_add_event_cb(btn, animal_item_wrapper_cb, LV_EVENT_ALL, ctx);
      }
      core_free_animal_list(animals);
    }
  }
}

static void search_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  // Show keyboard on focus
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    if (kb) {
      lv_keyboard_set_textarea(kb, ta_search);
      lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (code ==
             LV_EVENT_VALUE_CHANGED) { // || code == LV_EVENT_DEFOCUSED) {
    const char *txt = lv_textarea_get_text(ta_search);
    load_animal_list_correct(txt);
  }
}

static void back_event_cb(lv_event_t *e) {
  ui_nav_navigate(UI_SCREEN_DASHBOARD, true);
}

static void add_animal_event_cb(lv_event_t *e) {
  ui_nav_navigate_ctx(UI_SCREEN_ANIMAL_FORM, NULL, true);
}

static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t *ui_create_animal_list_screen(void) {
  ESP_LOGI("UI_ANIMALS", "Creating Animal List Screen");
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;
  const lv_coord_t search_bar_height = 40;
  const lv_coord_t vertical_margin = 20;

  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "animals");

  // Header Helper
  ui_helper_create_header(scr, "Mes Animaux", back_event_cb, "Retour");

  // Add Button in header (manual placement as helper is generic)
  // Actually helper returns header object, so we can add children to it!
  // But header children are not exposed easily if helper hides logic.
  // Let's add over it absolutely or get child from screen (child 0 is header
  // usually). Or just create a button on screen aligned to top right.
  lv_obj_t *btn_add = lv_button_create(scr);
  lv_obj_align(btn_add, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_size(btn_add, 100, 40);
  lv_obj_set_style_bg_color(btn_add, lv_palette_darken(LV_PALETTE_GREEN, 2), 0);
  lv_obj_add_event_cb(btn_add, add_animal_event_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS " Ajouter");

  // Search Bar
  ta_search = lv_textarea_create(scr);
  lv_textarea_set_one_line(ta_search, true);
  lv_textarea_set_placeholder_text(ta_search, "Rechercher...");
  lv_obj_set_size(ta_search, LV_PCT(90), search_bar_height);
  lv_obj_align(ta_search, LV_ALIGN_TOP_MID, 0, header_height + 10);
  lv_obj_add_event_cb(ta_search, search_event_cb, LV_EVENT_ALL, NULL);

  // Keyboard Helper
  kb = lv_keyboard_create(scr);
  ui_helper_setup_keyboard(kb); // AZERTY
  lv_keyboard_set_textarea(kb, ta_search);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

  // List
  list_animals = lv_list_create(scr);
  lv_coord_t list_h = disp_h - header_height - search_bar_height -
                      vertical_margin - 30; // some padding
  lv_obj_set_size(list_animals, disp_w, list_h);
  lv_obj_set_y(list_animals,
               header_height + search_bar_height + vertical_margin + 10);

  load_animal_list_correct(NULL);

  ESP_LOGI("UI_ANIMALS", "Loading screen...");
  return scr;
}