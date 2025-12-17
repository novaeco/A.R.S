#include "ui_animal_details.h"
#include "core_service.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_animal_form.h"
#include "../ui_screen_manager.h"
#include "../ui_theme.h"
#include "ui_animals.h"
#include "ui_reproduction.h"
#if defined(LV_USE_QRCODE) && LV_USE_QRCODE
// #include "lv_qrcode.h" // Removed to fix build error; rely on lvgl.h or
// internal
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char current_animal_id[37];
static lv_obj_t *scr_details;
static lv_obj_t *tabview;

// =============================================================================
// Helpers
// =============================================================================

static void format_date(char *buf, size_t len, uint32_t timestamp) {
  if (timestamp == 0) {
    snprintf(buf, len, "N/A");
    return;
  }
  time_t rawtime = (time_t)timestamp;
  struct tm *timeinfo = localtime(&rawtime);
  strftime(buf, len, "%d/%m/%Y", timeinfo);
}

static void back_event_cb(lv_event_t *e) { ui_create_animal_list_screen(); }

static void edit_event_cb(lv_event_t *e) {
  ui_create_animal_form_screen(current_animal_id);
}

static void repro_event_cb(lv_event_t *e) {
  ui_create_reproduction_screen(current_animal_id);
}

// =============================================================================
// QR Code Logic
// =============================================================================

static lv_obj_t *mbox_qr;

static void close_qr_cb(lv_event_t *e) { lv_msgbox_close(mbox_qr); }

static void qr_btn_cb(lv_event_t *e) {
  mbox_qr = lv_msgbox_create(scr_details);
  lv_msgbox_add_title(mbox_qr, "QR Code Animal");
  lv_msgbox_add_close_button(mbox_qr);
  lv_obj_center(mbox_qr);

#if defined(LV_USE_QRCODE) && LV_USE_QRCODE
  // Create QR Code
  // Size: 150px, Dark color: Black, Light color: White
  lv_obj_t *qr = lv_qrcode_create(mbox_qr);
  lv_qrcode_set_size(qr, 150);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());

  // Data: URL to local API or just ID
  char data[128];
  // Assuming standard IP or mDNS. For now, just a fake URL structure.
  snprintf(data, sizeof(data), "http://esp32-reptile.local/api/animals/%s",
           current_animal_id);
  lv_qrcode_update(qr, data, strlen(data));
  lv_obj_center(qr);
#else
  lv_msgbox_add_text(mbox_qr, current_animal_id);
#endif

  lv_obj_t *btn = lv_button_create(mbox_qr);
  lv_obj_add_event_cb(btn, close_qr_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn), "Fermer");
}

// =============================================================================
// Add Weight Logic
// =============================================================================

static lv_obj_t *mbox_weight;
static lv_obj_t *ta_weight;

static void save_weight_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(ta_weight);
  if (strlen(txt) > 0) {
    float val = atof(txt);
    if (core_add_weight(current_animal_id, val, "g") == ESP_OK) {
      LV_LOG_USER("Weight added");
      lv_msgbox_close(mbox_weight);
      ui_create_animal_details_screen(current_animal_id); // Reload
    }
  }
}

static void add_weight_btn_cb(lv_event_t *e) {
  mbox_weight = lv_msgbox_create(scr_details);
  lv_msgbox_add_title(mbox_weight, "Ajouter Poids");
  lv_msgbox_add_text(mbox_weight, "Entrez le poids en grammes:");
  lv_msgbox_add_close_button(mbox_weight);
  lv_obj_center(mbox_weight);

  ta_weight = lv_textarea_create(mbox_weight);
  lv_textarea_set_one_line(ta_weight, true);
  lv_textarea_set_accepted_chars(ta_weight, "0123456789.");
  lv_obj_set_width(ta_weight, 200);

  lv_obj_t *kb = lv_keyboard_create(scr_details);
  lv_keyboard_set_textarea(kb, ta_weight);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);

  lv_obj_t *btn = lv_button_create(mbox_weight);
  lv_obj_add_event_cb(btn, save_weight_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn), "Sauvegarder");
}

// =============================================================================
// Add Event Logic
// =============================================================================

static lv_obj_t *mbox_event;
static lv_obj_t *dd_event_type;
static lv_obj_t *ta_event_desc;

static void save_event_cb(lv_event_t *e) {
  char buf[32];
  lv_dropdown_get_selected_str(dd_event_type, buf, sizeof(buf));
  const char *desc = lv_textarea_get_text(ta_event_desc);

  event_type_t type = EVENT_OTHER;
  if (strcmp(buf, "Nourrissage") == 0)
    type = EVENT_FEEDING;
  else if (strcmp(buf, "Mue") == 0)
    type = EVENT_SHEDDING;
  else if (strcmp(buf, "Veterinaire") == 0)
    type = EVENT_VET;
  else if (strcmp(buf, "Nettoyage") == 0)
    type = EVENT_CLEANING;

  if (core_add_event(current_animal_id, type, desc) == ESP_OK) {
    LV_LOG_USER("Event added");
    lv_msgbox_close(mbox_event);
    ui_create_animal_details_screen(current_animal_id); // Reload
  }
}

static void add_event_btn_cb(lv_event_t *e) {
  mbox_event = lv_msgbox_create(scr_details);
  lv_msgbox_add_title(mbox_event, "Ajouter Evenement");
  lv_msgbox_add_close_button(mbox_event);
  lv_obj_set_size(mbox_event, 400, 300);
  lv_obj_center(mbox_event);

  dd_event_type = lv_dropdown_create(mbox_event);
  lv_dropdown_set_options(dd_event_type,
                          "Nourrissage\nMue\nVeterinaire\nNettoyage\nAutre");
  lv_obj_set_width(dd_event_type, LV_PCT(100));

  ta_event_desc = lv_textarea_create(mbox_event);
  lv_textarea_set_placeholder_text(ta_event_desc, "Description...");
  lv_obj_set_size(ta_event_desc, LV_PCT(100), 100);

  lv_obj_t *kb = lv_keyboard_create(scr_details);
  lv_keyboard_set_textarea(kb, ta_event_desc);

  lv_obj_t *btn = lv_button_create(mbox_event);
  lv_obj_add_event_cb(btn, save_event_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn), "Sauvegarder");
}

// =============================================================================
// Tab Builders
// =============================================================================

static void build_info_tab(lv_obj_t *parent, const animal_t *animal) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW); // Row layout for Image + Info
  lv_obj_set_style_border_width(cont, 0, 0);

  // Left: Image
  lv_obj_t *img_cont = lv_obj_create(cont);
  lv_obj_set_size(img_cont, LV_PCT(40), LV_PCT(100));
  lv_obj_set_flex_flow(img_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_border_width(img_cont, 0, 0);

  // Photo
  char img_path[64];
  snprintf(img_path, sizeof(img_path), "S:/images/%s.jpg",
           animal->id); // 'S:' driver letter for FS
  // Note: LVGL file system driver must be registered with letter 'S' or
  // similar. Assuming "S:/..." maps to SD card. If not, we might need standard
  // path "/sdcard/..." depending on how lv_fs is configured. Standard ESP-IDF
  // LVGL often uses "/sdcard" directly if POSIX driver used, OR "S:..." if LVGL
  // specific driver used. Let's try standard path first, or assume "A:"/"S:".
  // For safety in this environment, I'll use the path string directly, assuming
  // the FS driver handles it. Actually, standard LVGL FS uses "L:/..." where L
  // is letter. Let's try "S:/sdcard/animals/..." assuming 'S' is registered for
  // SD. If file doesn't exist, show placeholder.

  lv_obj_t *img = lv_img_create(img_cont);
  // Check for file (assuming S: is mounted via FATFS/SDSPI)
  // If not found, show a large icon
  FILE *f = fopen(img_path, "r");
  if (f) {
    fclose(f);
    lv_img_set_src(img, img_path);
  } else {
    // Audit Fix: Try placeholder
    const char *ph_path = "S:/images/placeholder.jpg";
    FILE *f_ph = fopen(ph_path, "r");
    if (f_ph) {
      fclose(f_ph);
      lv_img_set_src(img, ph_path);
    } else {
      // Audit Fix: Better Fallback UI
      lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);

      lv_obj_t *ph_cont = lv_obj_create(img_cont);
      lv_obj_set_size(ph_cont, LV_PCT(100),
                      200); // Fixed height for placeholder
      lv_obj_set_style_bg_color(ph_cont, lv_palette_main(LV_PALETTE_GREY), 0);
      lv_obj_set_style_bg_opa(ph_cont, LV_OPA_20, 0);
      lv_obj_center(ph_cont);

      lv_obj_t *ph = lv_label_create(ph_cont);
      lv_label_set_text(ph, LV_SYMBOL_IMAGE "\nNo Photo");
      lv_obj_set_style_text_align(ph, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_color(ph, lv_palette_main(LV_PALETTE_GREY), 0);
      lv_obj_center(ph);
    }
  }
  lv_obj_set_width(img, LV_PCT(100));
  lv_obj_set_height(img, LV_SIZE_CONTENT);

  // QR Code Button below image
  lv_obj_t *btn_qr = lv_button_create(img_cont);
  lv_obj_set_width(btn_qr, LV_PCT(100));
  // Audit Fix: Pressed State
  lv_obj_set_style_bg_color(btn_qr, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_qr, qr_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_qr), LV_SYMBOL_EYE_OPEN " QR Code");

  // Right: Info
  lv_obj_t *info_cont = lv_obj_create(cont);
  lv_obj_set_size(info_cont, LV_PCT(60), LV_PCT(100));
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_border_width(info_cont, 0, 0);

  char buf[128];

  lv_label_set_text_fmt(lv_label_create(info_cont), "Nom: %s", animal->name);
  lv_label_set_text_fmt(lv_label_create(info_cont), "Espece: %s",
                        animal->species);

  const char *sex_str =
      (animal->sex == SEX_MALE)
          ? "Male"
          : ((animal->sex == SEX_FEMALE) ? "Femelle" : "Inconnu");
  lv_label_set_text_fmt(lv_label_create(info_cont), "Sexe: %s", sex_str);

  format_date(buf, sizeof(buf), animal->dob);
  lv_label_set_text_fmt(lv_label_create(info_cont), "Date Naissance: %s", buf);

  lv_label_set_text_fmt(lv_label_create(info_cont), "Origine: %s",
                        animal->origin);
  lv_label_set_text_fmt(lv_label_create(info_cont), "I-FAP: %s",
                        animal->registry_id);

  // Edit Button
  lv_obj_t *btn_edit = lv_button_create(info_cont);
  lv_obj_set_style_bg_color(btn_edit, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED); // Pressed
  lv_obj_add_event_cb(btn_edit, edit_event_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_edit),
                    LV_SYMBOL_EDIT " Modifier Fiche");

  // Reproduction Button
  lv_obj_t *btn_repro = lv_button_create(info_cont);
  lv_obj_add_event_cb(btn_repro, repro_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_bg_color(btn_repro, lv_palette_main(LV_PALETTE_PINK), 0);
  lv_obj_set_style_bg_color(btn_repro, lv_palette_darken(LV_PALETTE_PINK, 2),
                            LV_STATE_PRESSED); // Pressed
  lv_obj_t *lbl_repro = lv_label_create(btn_repro);
  lv_label_set_text(lbl_repro, LV_SYMBOL_LOOP " Reproduction");
  lv_obj_set_style_text_color(lbl_repro, lv_color_black(),
                              0); // Audit Fix: Contrast
}

static void build_weight_tab(lv_obj_t *parent, const animal_t *animal) {
  lv_obj_t *btn_add = lv_button_create(parent);
  lv_obj_set_size(btn_add, 40, 40);
  lv_obj_set_style_bg_color(btn_add, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED); // Pressed
  lv_obj_align(btn_add, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS);
  lv_obj_add_event_cb(btn_add, add_weight_btn_cb, LV_EVENT_CLICKED, NULL);

  // Chart Container
  lv_obj_t *chart_cont = lv_obj_create(parent);
  lv_obj_set_size(chart_cont, LV_PCT(100), LV_PCT(50));
  lv_obj_set_y(chart_cont, 50);
  lv_obj_set_style_border_width(chart_cont, 0, 0);

  if (animal->weight_count > 0) {
    lv_obj_t *chart = lv_chart_create(chart_cont);
    lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
    lv_obj_center(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, animal->weight_count);

    lv_chart_series_t *ser = lv_chart_add_series(
        chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    for (size_t i = 0; i < animal->weight_count; i++) {
      lv_chart_set_next_value(chart, ser, (lv_coord_t)animal->weights[i].value);
    }
    lv_chart_refresh(chart);
  } else {
    lv_label_set_text(lv_label_create(chart_cont),
                      "Pas de données graphiques.");
  }

  // List Container
  lv_obj_t *list = lv_list_create(parent);
  lv_obj_set_size(list, LV_PCT(100), LV_PCT(35));
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  if (animal->weight_count == 0) {
    lv_list_add_text(list, "Aucun poids enregistré.");
  } else {
    char buf[64];
    for (int i = animal->weight_count - 1; i >= 0; i--) {
      format_date(buf, sizeof(buf), animal->weights[i].date);
      char item_str[128];
      snprintf(item_str, sizeof(item_str), "%s: %.1f %s", buf,
               animal->weights[i].value, animal->weights[i].unit);
      lv_list_add_btn(list, NULL, item_str);
    }
  }
}

static void build_event_tab(lv_obj_t *parent, const animal_t *animal) {
  lv_obj_t *btn_add = lv_button_create(parent);
  lv_obj_set_size(btn_add, 40, 40);
  lv_obj_set_style_bg_color(btn_add, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED); // Pressed
  lv_obj_align(btn_add, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS);
  lv_obj_add_event_cb(btn_add, add_event_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *list = lv_list_create(parent);
  lv_obj_set_size(list, LV_PCT(100), LV_PCT(85));
  lv_obj_set_y(list, 50);

  if (animal->event_count == 0) {
    lv_list_add_text(list, "Aucun événement.");
  } else {
    char buf[64];
    const char *type_str;
    for (int i = animal->event_count - 1; i >= 0; i--) {
      format_date(buf, sizeof(buf), animal->events[i].date);

      switch (animal->events[i].type) {
      case EVENT_FEEDING:
        type_str = "Nourrissage";
        break;
      case EVENT_SHEDDING:
        type_str = "Mue";
        break;
      case EVENT_VET:
        type_str = "Veto";
        break;
      case EVENT_CLEANING:
        type_str = "Nettoyage";
        break;
      case EVENT_MATING:
        type_str = "Accouplement";
        break;
      case EVENT_LAYING:
        type_str = "Ponte";
        break;
      case EVENT_HATCHING:
        type_str = "Eclosion";
        break;
      default:
        type_str = "Autre";
        break;
      }

      char item_str[256];
      snprintf(item_str, sizeof(item_str), "%s [%s] %s", buf, type_str,
               animal->events[i].description);
      lv_list_add_btn(list, NULL, item_str);
    }
  }
}

// =============================================================================
// Main Create
// =============================================================================

void ui_create_animal_details_screen(const char *animal_id) {
  strlcpy(current_animal_id, animal_id, sizeof(current_animal_id));

  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  animal_t animal;
  if (core_get_animal(animal_id, &animal) != ESP_OK) {
    LV_LOG_ERROR("Failed to load animal %s", animal_id);
    return;
  }

  scr_details = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr_details, "animal_details");
  lv_obj_set_style_bg_color(scr_details, UI_COLOR_PAGE_BG, 0);

  // Header
  lv_obj_t *header = lv_obj_create(scr_details);
  lv_obj_set_size(header, LV_PCT(100), header_height);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_palette_darken(LV_PALETTE_GREY, 3), 0);

  lv_obj_t *btn_back = lv_button_create(header);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_back, lv_palette_darken(LV_PALETTE_BLUE, 2),
                            LV_STATE_PRESSED); // Pressed
  lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, animal.name);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // Tab View
  tabview = lv_tabview_create(scr_details);
  lv_obj_set_size(tabview, disp_w, disp_h - header_height);
  lv_obj_set_y(tabview, header_height);

  lv_obj_t *t1 = lv_tabview_add_tab(tabview, "Info");
  lv_obj_t *t2 = lv_tabview_add_tab(tabview, "Poids");
  lv_obj_t *t3 = lv_tabview_add_tab(tabview, "Journal");

  build_info_tab(t1, &animal);
  build_weight_tab(t2, &animal);
  build_event_tab(t3, &animal);

  core_free_animal_content(&animal);
  lv_screen_load(scr_details);
}