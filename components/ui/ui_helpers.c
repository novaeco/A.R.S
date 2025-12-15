#include "ui_helpers.h"
#include "ui.h" // For ui_battery_cb_t
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include <stdio.h>


static ui_battery_cb_t s_battery_cb = NULL;

void ui_set_battery_cb(ui_battery_cb_t cb) { s_battery_cb = cb; }

int ui_get_battery_level(uint8_t *percent, uint16_t *voltage_mv) {
  if (s_battery_cb) {
    return s_battery_cb(percent, voltage_mv);
  }
  return -1; // Error or Not Implemented
}

// ======================= SPINNER (delegated to screen manager) =======================
void ui_helper_show_spinner(void) { ui_show_loading(true); }

void ui_helper_hide_spinner(void) { ui_show_loading(false); }

// ======================= HEADER =======================
lv_obj_t *ui_helper_create_header(lv_obj_t *parent, const char *title,
                                  lv_event_cb_t back_cb,
                                  const char *back_text) {
  lv_obj_t *header = lv_obj_create(parent);
  lv_obj_set_size(header, LV_PCT(100), UI_HEADER_HEIGHT);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(header, &ui_style_card, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_shadow_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, UI_SPACE_MD, 0);
  lv_obj_set_style_pad_gap(header, UI_SPACE_MD, 0);
  lv_obj_set_style_bg_color(header, UI_COLOR_PRIMARY, 0);

  if (back_cb) {
    lv_obj_t *btn_back = lv_button_create(header);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, UI_SPACE_SM, 0);
    lv_obj_add_style(btn_back, &ui_style_btn_secondary, 0); // Use theme style
    lv_obj_set_style_pad_left(btn_back, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_right(btn_back, UI_SPACE_MD, 0);

    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn_back);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %s", LV_SYMBOL_LEFT,
             back_text ? back_text : "Retour");
    lv_label_set_text(lbl, buf);
    lv_obj_add_style(lbl, &ui_style_text_body, 0);
  }

  lv_obj_t *lbl_title = lv_label_create(header);
  lv_label_set_text(lbl_title, title);
  lv_obj_add_style(lbl_title, &ui_style_title, 0);
  lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  return header;
}

// ======================= KEYBOARD =======================
static const char *kb_map_azerty_lc[] = {"a",
                                         "z",
                                         "e",
                                         "r",
                                         "t",
                                         "y",
                                         "u",
                                         "i",
                                         "o",
                                         "p",
                                         LV_SYMBOL_BACKSPACE,
                                         "\n",
                                         "q",
                                         "s",
                                         "d",
                                         "f",
                                         "g",
                                         "h",
                                         "j",
                                         "k",
                                         "l",
                                         "m",
                                         LV_SYMBOL_NEW_LINE,
                                         "\n",
                                         "w",
                                         "x",
                                         "c",
                                         "v",
                                         "b",
                                         "n",
                                         ",",
                                         ".",
                                         ";",
                                         ":",
                                         "!",
                                         "\n",
                                         LV_SYMBOL_KEYBOARD,
                                         " ",
                                         LV_SYMBOL_OK,
                                         ""};

static const char *kb_map_azerty_uc[] = {"A",
                                         "Z",
                                         "E",
                                         "R",
                                         "T",
                                         "Y",
                                         "U",
                                         "I",
                                         "O",
                                         "P",
                                         LV_SYMBOL_BACKSPACE,
                                         "\n",
                                         "Q",
                                         "S",
                                         "D",
                                         "F",
                                         "G",
                                         "H",
                                         "J",
                                         "K",
                                         "L",
                                         "M",
                                         LV_SYMBOL_NEW_LINE,
                                         "\n",
                                         "W",
                                         "X",
                                         "C",
                                         "V",
                                         "B",
                                         "N",
                                         ",",
                                         ".",
                                         ";",
                                         ":",
                                         "!",
                                         "\n",
                                         LV_SYMBOL_KEYBOARD,
                                         " ",
                                         LV_SYMBOL_OK,
                                         ""};

static const lv_buttonmatrix_ctrl_t kb_ctrl_azerty_lc_map[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4,  4,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 16, 2};

static const lv_buttonmatrix_ctrl_t kb_ctrl_azerty_uc_map[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4,  4,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 16, 2};

void ui_helper_setup_keyboard(lv_obj_t *kb) {
  if (!kb)
    return;
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_azerty_lc,
                      kb_ctrl_azerty_lc_map);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_azerty_uc,
                      kb_ctrl_azerty_uc_map);
}
