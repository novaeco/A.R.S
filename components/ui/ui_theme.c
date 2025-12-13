#include "ui_theme.h"

// Global Styles
lv_style_t ui_style_screen;
lv_style_t ui_style_card;
lv_style_t ui_style_title;
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
  lv_style_set_radius(&ui_style_card, 12);
  lv_style_set_shadow_color(&ui_style_card, lv_color_make(0, 0, 0));
  lv_style_set_shadow_width(&ui_style_card, 20);
  lv_style_set_shadow_opa(&ui_style_card, LV_OPA_10);
  lv_style_set_shadow_spread(&ui_style_card, 0);
  lv_style_set_border_width(&ui_style_card, 0);

  // 3. Title Style
  lv_style_init(&ui_style_title);
  lv_style_set_text_color(&ui_style_title, UI_COLOR_TEXT_MAIN);
  // Assuming default font for now, ideally set bigger font if available
  // lv_style_set_text_font(&ui_style_title, &lv_font_montserrat_24);

  // 4. Primary Button Style
  lv_style_init(&ui_style_btn_primary);
  lv_style_set_bg_color(&ui_style_btn_primary, UI_COLOR_PRIMARY);
  lv_style_set_bg_opa(&ui_style_btn_primary, LV_OPA_COVER);
  lv_style_set_radius(&ui_style_btn_primary, 8);
  lv_style_set_text_color(&ui_style_btn_primary, lv_color_white());
  lv_style_set_shadow_width(&ui_style_btn_primary, 0);
  // Pressed
  // Disabled

  // 5. Secondary Button Style (or Alert)
  lv_style_init(&ui_style_btn_secondary);
  lv_style_set_bg_color(&ui_style_btn_secondary, UI_COLOR_SECONDARY);
  lv_style_set_bg_opa(&ui_style_btn_secondary, LV_OPA_COVER);
  lv_style_set_radius(&ui_style_btn_secondary, 8);
  lv_style_set_text_color(&ui_style_btn_secondary, lv_color_white());
  // Pressed
  // Disabled

  // 6. Global Theme (LVGL Default)
  lv_display_t *disp = lv_display_get_default();
  if (disp) {
    lv_theme_t *theme =
        lv_theme_default_init(disp, UI_COLOR_PRIMARY, UI_COLOR_SECONDARY,
                              false, // Light
                              LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
  }
}

void ui_theme_apply(lv_obj_t *obj) {
  if (!obj)
    return;
  lv_obj_add_style(obj, &ui_style_screen, 0);
}
