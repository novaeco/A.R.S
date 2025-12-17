#include "ui_animal_form.h"
#include "../ui_helpers.h"
#include "../ui_screen_manager.h"
#include "../ui_theme.h"
#include "core_service.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_animals.h" // For loading list after save
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// UI Objects
static lv_obj_t *ta_name;
static lv_obj_t *ta_species;
static lv_obj_t *ta_dob;
static lv_obj_t *dd_sex;
static lv_obj_t *ta_origin;
static lv_obj_t *ta_icad;
static lv_obj_t *kb;

// Current animal ID (if editing)
static char current_animal_id[37];
static bool is_edit_mode = false;

// Calendar cb
static void calendar_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *cal = lv_event_get_target(e);
  lv_obj_t *parent = lv_obj_get_parent(cal);

  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(cal, &date)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%02d/%02d/%d", date.day, date.month,
               date.year);
      lv_textarea_set_text(ta_dob, buf);

      // Delete modal
      lv_obj_del(parent);
    }
  }
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {

    // DOB Calendar Logic
    if (ta == ta_dob) {
      if (kb != NULL)
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

      lv_obj_t *cal_cont = lv_obj_create(lv_screen_active());
      lv_obj_set_size(cal_cont, 300, 300);
      lv_obj_center(cal_cont);

      lv_obj_t *cal = lv_calendar_create(cal_cont);
      lv_obj_set_size(cal, 280, 280);
      lv_obj_center(cal);
      // Set default date?
      lv_calendar_set_today_date(cal, 2023, 1, 1);
      lv_calendar_set_showed_date(cal, 2023, 1);
      lv_obj_add_event_cb(cal, calendar_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

      lv_calendar_header_arrow_create(cal);
      return;
    }

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

static void back_event_cb(lv_event_t *e) {
  // If editing, go back to details? Else list.
  // Navigation stack would be better, but for now simple check.
  if (is_edit_mode) {
    // Ideally go to details, but we didn't include details header here
    // directly... Let's go to list, safe default.
    ui_create_animal_list_screen();
  } else {
    ui_create_animal_list_screen();
  }
}

static void delete_cancel_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_del(mbox);
}

static void delete_ok_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  core_delete_animal(current_animal_id);
  lv_obj_del(mbox);
  ui_create_animal_list_screen();
}

static void delete_btn_cb(lv_event_t *e) {
  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, "Confirmation");
  lv_msgbox_add_text(mbox, "Voulez-vous vraiment supprimer cet animal ?");

  // Custom Buttons for Delete
  lv_obj_t *btn_del = lv_msgbox_add_footer_button(mbox, "Supprimer");
  lv_obj_set_style_bg_color(btn_del, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(btn_del, delete_ok_cb, LV_EVENT_CLICKED, mbox);

  lv_obj_t *btn_cancel = lv_msgbox_add_footer_button(mbox, "Annuler");
  lv_obj_add_event_cb(btn_cancel, delete_cancel_cb, LV_EVENT_CLICKED, mbox);

  lv_obj_center(mbox);
}

static void save_btn_cb(lv_event_t *e) {
  const char *name = lv_textarea_get_text(ta_name);
  if (strlen(name) == 0) {
    ui_show_toast("Le nom est obligatoire", UI_TOAST_ERROR);
    return;
  }

  animal_t animal;
  memset(&animal, 0, sizeof(animal));

  if (is_edit_mode) {
    strlcpy(animal.id, current_animal_id, sizeof(animal.id));
  } else {
    // Generate ID or let core handle it? Core expects us to pass one usually or
    // generates if empty? Core_save_animal logic: if ID empty/zero, assume new.
  }

  strlcpy(animal.name, name, sizeof(animal.name));
  strlcpy(animal.species, lv_textarea_get_text(ta_species),
          sizeof(animal.species));

  // Parse DOB (DD/MM/YYYY)
  const char *dob_str = lv_textarea_get_text(ta_dob);
  int d, m, y;
  if (sscanf(dob_str, "%d/%d/%d", &d, &m, &y) == 3) {
    struct tm tm_info = {0};
    tm_info.tm_year = y - 1900;
    tm_info.tm_mon = m - 1;
    tm_info.tm_mday = d;
    animal.dob = mktime(&tm_info);
  }

  char sex_buf[16];
  lv_dropdown_get_selected_str(dd_sex, sex_buf, sizeof(sex_buf));
  if (strcmp(sex_buf, "Male") == 0)
    animal.sex = SEX_MALE;
  else if (strcmp(sex_buf, "Femelle") == 0)
    animal.sex = SEX_FEMALE;
  else
    animal.sex = SEX_UNKNOWN;

  strlcpy(animal.origin, lv_textarea_get_text(ta_origin),
          sizeof(animal.origin));
  strlcpy(animal.registry_id, lv_textarea_get_text(ta_icad),
          sizeof(animal.registry_id));

  if (core_save_animal(&animal) == ESP_OK) {
    ui_create_animal_list_screen();
  } else {
    ui_show_toast("Echec de la sauvegarde", UI_TOAST_ERROR);
  }
}

void ui_create_animal_form_screen(const char *edit_id) {
  if (edit_id) {
    is_edit_mode = true;
    strlcpy(current_animal_id, edit_id, sizeof(current_animal_id));
  } else {
    is_edit_mode = false;
    current_animal_id[0] = '\0';
  }

  lv_display_t *disp = lv_display_get_default();
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t header_height = 60;

  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "animal_form");

  // Header Helper
  const char *title_txt = is_edit_mode ? "Modifier Animal" : "Nouvel Animal";
  ui_helper_create_header(scr, title_txt, back_event_cb, "Annuler");

  // Content
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, disp_w,
                  disp_h - header_height /*- 200*/); // Leave space for KB?
  lv_obj_set_y(cont, header_height);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // 1. Name
  lv_textarea_create(cont); // spacer? No.
  ta_name = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_name, "Nom");
  lv_textarea_set_one_line(ta_name, true);
  lv_obj_add_event_cb(ta_name, ta_event_cb, LV_EVENT_ALL, NULL);

  // 2. Species
  ta_species = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_species, "Espece (ex: Python Regius)");
  lv_textarea_set_one_line(ta_species, true);
  lv_obj_add_event_cb(ta_species, ta_event_cb, LV_EVENT_ALL, NULL);

  // 3. DOB (Calendar)
  ta_dob = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_dob, "Date Naissance (JJ/MM/AAAA)");
  lv_textarea_set_one_line(ta_dob, true);
  lv_obj_add_event_cb(ta_dob, ta_event_cb, LV_EVENT_ALL, NULL);

  // 4. Sex
  // Label?
  lv_obj_t *lbl_sex = lv_label_create(cont);
  lv_label_set_text(lbl_sex, "Sexe:");
  dd_sex = lv_dropdown_create(cont);
  lv_dropdown_set_options(dd_sex, "Inconnu\nMale\nFemelle");

  // 5. Origin
  ta_origin = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_origin, "Provenance / Phase");
  lv_textarea_set_one_line(ta_origin, true);
  lv_obj_add_event_cb(ta_origin, ta_event_cb, LV_EVENT_ALL, NULL);

  // 6. ICAD/Registry
  ta_icad = lv_textarea_create(cont);
  lv_textarea_set_placeholder_text(ta_icad, "Numero ICAD / Registre");
  lv_textarea_set_one_line(ta_icad, true);
  lv_obj_add_event_cb(ta_icad, ta_event_cb, LV_EVENT_ALL, NULL);

  // Save Button
  lv_obj_t *btn_save = lv_button_create(cont);
  lv_obj_set_style_bg_color(btn_save, lv_palette_darken(LV_PALETTE_GREEN, 2),
                            0);
  lv_obj_add_event_cb(btn_save, save_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_save), LV_SYMBOL_SAVE " Sauvegarder");

  if (is_edit_mode) {
    lv_obj_t *btn_del = lv_button_create(cont);
    lv_obj_set_style_bg_color(btn_del, lv_palette_darken(LV_PALETTE_RED, 2), 0);
    lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_del), LV_SYMBOL_TRASH " Supprimer");
  }

  // Pre-fill if edit
  if (is_edit_mode) {
    animal_t anim;
    if (core_get_animal(current_animal_id, &anim) == ESP_OK) {
      lv_textarea_set_text(ta_name, anim.name);
      lv_textarea_set_text(ta_species, anim.species);
      lv_textarea_set_text(ta_origin, anim.origin);
      lv_textarea_set_text(ta_icad, anim.registry_id);

      if (anim.sex == SEX_MALE)
        lv_dropdown_set_selected(dd_sex, 1);
      else if (anim.sex == SEX_FEMALE)
        lv_dropdown_set_selected(dd_sex, 2);
      else
        lv_dropdown_set_selected(dd_sex, 0);

      if (anim.dob > 0) {
        struct tm *t = localtime((time_t *)&anim.dob);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d/%02d/%d", t->tm_mday, t->tm_mon + 1,
                 t->tm_year + 1900);
        lv_textarea_set_text(ta_dob, buf);
      }
      core_free_animal_content(&anim);
    }
  }

  // Keyboard Helper
  kb = lv_keyboard_create(scr);
  ui_helper_setup_keyboard(kb);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_screen_load(scr);
}