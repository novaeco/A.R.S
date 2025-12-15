#include "ui_theme.h"

// Global Styles
lv_style_t ui_style_screen;
lv_style_t ui_style_card;
lv_style_t ui_style_title;
lv_style_t ui_style_text_body;
lv_style_t ui_style_text_muted;
lv_style_t ui_style_btn_primary;
lv_style_t ui_style_btn_secondary;

void ui_theme_init(void) {
  // 1. Screen Style (Background)
  lv_style_init(&ui_style_screen);
  lv_style_set_bg_color(&ui_style_screen, UI_COLOR_BACKGROUND);
  lv_style_set_bg_opa(&ui_style_screen, LV_OPA_COVER);

  // 2. Card/Tile Style (White, Radius, Shadow)
  lv_style_init(&ui_style_card);
  lv_style_set_bg_color(&ui_style_card, UI_COLOR_CARD);
  lv_style_set_bg_opa(&ui_style_card, LV_OPA_COVER);
  lv_style_set_radius(&ui_style_card, UI_RADIUS_MD);
  lv_style_set_shadow_color(&ui_style_card, lv_color_make(0, 0, 0));
  lv_style_set_shadow_width(&ui_style_card, UI_SHADOW_WIDTH);
  lv_style_set_shadow_opa(&ui_style_card, UI_SHADOW_OPA);
  lv_style_set_shadow_spread(&ui_style_card, 0);
  lv_style_set_border_width(&ui_style_card, 0);
  lv_style_set_pad_all(&ui_style_card, UI_SPACE_LG);

  // 3. Title Style
  lv_style_init(&ui_style_title);
  lv_style_set_text_color(&ui_style_title, UI_COLOR_TEXT_MAIN);
  lv_style_set_text_font(&ui_style_title, UI_FONT_TITLE);
  lv_style_set_text_letter_space(&ui_style_title, 1);

  // 4. Body Text Style
  lv_style_init(&ui_style_text_body);
  lv_style_set_text_color(&ui_style_text_body, UI_COLOR_TEXT_MAIN);
  lv_style_set_text_font(&ui_style_text_body, UI_FONT_BODY);

  // 5. Muted Text Style
  lv_style_init(&ui_style_text_muted);
  lv_style_set_text_color(&ui_style_text_muted, UI_COLOR_TEXT_MUTED);
  lv_style_set_text_font(&ui_style_text_muted, UI_FONT_SMALL);

  // 6. Primary Button Style
  lv_style_init(&ui_style_btn_primary);
  lv_style_set_bg_color(&ui_style_btn_primary, UI_COLOR_PRIMARY);
  lv_style_set_bg_opa(&ui_style_btn_primary, LV_OPA_COVER);
  lv_style_set_radius(&ui_style_btn_primary, UI_RADIUS_SM);
  lv_style_set_text_color(&ui_style_btn_primary, lv_color_white());
  lv_style_set_text_font(&ui_style_btn_primary, UI_FONT_BODY);
  lv_style_set_border_width(&ui_style_btn_primary, 0);
  lv_style_set_shadow_width(&ui_style_btn_primary, 0);

  // 7. Secondary Button Style (or Alert)
  lv_style_init(&ui_style_btn_secondary);
  lv_style_set_bg_color(&ui_style_btn_secondary, UI_COLOR_SECONDARY);
  lv_style_set_bg_opa(&ui_style_btn_secondary, LV_OPA_COVER);
  lv_style_set_radius(&ui_style_btn_secondary, UI_RADIUS_SM);
  lv_style_set_text_color(&ui_style_btn_secondary, UI_COLOR_TEXT_MAIN);
  lv_style_set_text_font(&ui_style_btn_secondary, UI_FONT_BODY);
  lv_style_set_border_width(&ui_style_btn_secondary, 0);
  lv_style_set_shadow_width(&ui_style_btn_secondary, 0);

  // 8. Global Theme (LVGL Default)
  lv_display_t *disp = lv_display_get_default();
  if (disp) {
    lv_theme_t *theme =
        lv_theme_default_init(disp, UI_COLOR_PRIMARY, UI_COLOR_SECONDARY,
                              false, // Light
                              UI_FONT_BODY);
    lv_display_set_theme(disp, theme);
  }
}

void ui_theme_apply(lv_obj_t *obj) {
  if (!obj)
    return;
  lv_obj_add_style(obj, &ui_style_screen, 0);
}
