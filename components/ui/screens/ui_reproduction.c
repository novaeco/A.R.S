#include "ui_reproduction.h"
#include "ui_animal_details.h"
#include "ui.h"
#include "core_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char current_animal_id[37];
static lv_obj_t * scr_repro;
static lv_obj_t * list_history;

// =============================================================================
// Helpers
// =============================================================================

static void format_date(char *buf, size_t len, uint32_t timestamp) {
    if (timestamp == 0) {
        snprintf(buf, len, "N/A");
        return;
    }
    time_t rawtime = (time_t)timestamp;
    struct tm * timeinfo = localtime(&rawtime);
    strftime(buf, len, "%d/%m/%Y", timeinfo);
}

static void back_event_cb(lv_event_t * e) {
    ui_create_animal_details_screen(current_animal_id);
}

// =============================================================================
// Add Reproduction Event Logic
// =============================================================================

static lv_obj_t * mbox_add;
static lv_obj_t * dd_type;
static lv_obj_t * ta_desc;

static void save_repro_event_cb(lv_event_t * e) {
    char buf[32];
    lv_dropdown_get_selected_str(dd_type, buf, sizeof(buf));
    const char * desc = lv_textarea_get_text(ta_desc);
    
    event_type_t type = EVENT_MATING;
    if (strcmp(buf, "Accouplement") == 0) type = EVENT_MATING;
    else if (strcmp(buf, "Ponte") == 0) type = EVENT_LAYING;
    else if (strcmp(buf, "Eclosion") == 0) type = EVENT_HATCHING;

    if (core_add_event(current_animal_id, type, desc) == ESP_OK) {
        LV_LOG_USER("Repro event added");
        lv_msgbox_close(mbox_add);
        ui_create_reproduction_screen(current_animal_id); // Reload
    }
}

static void add_btn_cb(lv_event_t * e) {
    mbox_add = lv_msgbox_create(scr_repro);
    lv_msgbox_add_title(mbox_add, "Ajouter Evenement");
    lv_msgbox_add_close_button(mbox_add);
    lv_obj_center(mbox_add);

    dd_type = lv_dropdown_create(mbox_add);
    lv_dropdown_set_options(dd_type, "Accouplement\nPonte\nEclosion");
    lv_obj_set_width(dd_type, LV_PCT(100));

    ta_desc = lv_textarea_create(mbox_add);
    lv_textarea_set_placeholder_text(ta_desc, "Notes (ex: male partenaire, nb oeufs...)");
    lv_obj_set_size(ta_desc, LV_PCT(100), 80);
    
    lv_obj_t * kb = lv_keyboard_create(scr_repro);
    lv_keyboard_set_textarea(kb, ta_desc);

    lv_obj_t * btn = lv_button_create(mbox_add);
    lv_obj_add_event_cb(btn, save_repro_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Sauvegarder");
}

// =============================================================================
// Main Create
// =============================================================================

void ui_create_reproduction_screen(const char *animal_id) {
    strlcpy(current_animal_id, animal_id, sizeof(current_animal_id));

    lv_display_t *disp = lv_display_get_default();
    lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
    const lv_coord_t header_height = 60;

    animal_t animal;
    if (core_get_animal(animal_id, &animal) != ESP_OK) return;

    scr_repro = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_repro, lv_color_hex(0xF0F0F0), 0);

    // Header
    lv_obj_t * header = lv_obj_create(scr_repro);
    lv_obj_set_size(header, LV_PCT(100), header_height);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_palette_main(LV_PALETTE_PINK), 0); // Pink for reproduction

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text_fmt(title, "Reproduction: %s", animal.name);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * btn_add = lv_button_create(header);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(btn_add, add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS);

    // List
    list_history = lv_list_create(scr_repro);
    lv_obj_set_size(list_history, disp_w, disp_h - header_height);
    lv_obj_set_y(list_history, header_height);

    if (animal.event_count == 0) {
        lv_list_add_text(list_history, "Aucune donnée de reproduction.");
    } else {
        char buf[64];
        bool found = false;
        for (int i = animal.event_count - 1; i >= 0; i--) {
            event_type_t t = animal.events[i].type;
            if (t == EVENT_MATING || t == EVENT_LAYING || t == EVENT_HATCHING) {
                found = true;
                format_date(buf, sizeof(buf), animal.events[i].date);
                
                const char *type_str = "Inconnu";
                const char *icon = LV_SYMBOL_BULLET;
                if (t == EVENT_MATING) { type_str = "Accouplement"; icon = LV_SYMBOL_LOOP; }
                else if (t == EVENT_LAYING) { type_str = "Ponte"; icon = LV_SYMBOL_DOWNLOAD; }
                else if (t == EVENT_HATCHING) { type_str = "Eclosion"; icon = LV_SYMBOL_UP; }

                char item_str[256];
                snprintf(item_str, sizeof(item_str), "%s [%s] %s", buf, type_str, animal.events[i].description);
                lv_list_add_btn(list_history, icon, item_str);
            }
        }
        if (!found) lv_list_add_text(list_history, "Aucune donnée de reproduction.");
    }

    core_free_animal_content(&animal);
    lv_screen_load(scr_repro);
}