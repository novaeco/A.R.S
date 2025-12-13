#pragma once

#include "lvgl.h"

// =============================================================================
// Color Palette (Premium Nature Theme)
// =============================================================================
// Primary: Deep Forest Green
#define UI_COLOR_PRIMARY lv_color_hex(0x2E8B57)
// Secondary: Earth/Sand
#define UI_COLOR_SECONDARY lv_color_hex(0xD2B48C)
// Background: Soft Off-White
#define UI_COLOR_BACKGROUND lv_color_hex(0xF4F4F4)
// Card/Panel: White
#define UI_COLOR_CARD lv_color_hex(0xFFFFFF)
// Text: Dark Charcoal
#define UI_COLOR_TEXT_MAIN lv_color_hex(0x2C3E50)
// Text: Muted Grey
#define UI_COLOR_TEXT_MUTED lv_color_hex(0x7F8C8D)
// Alert: Burnt Orange
#define UI_COLOR_ALERT lv_color_hex(0xE67E22)

// =============================================================================
// Styles
// =============================================================================
extern lv_style_t ui_style_screen;
extern lv_style_t ui_style_card;
extern lv_style_t ui_style_title;
extern lv_style_t ui_style_btn_primary;
extern lv_style_t ui_style_btn_secondary;

// =============================================================================
// API
// =============================================================================
void ui_theme_init(void);
void ui_theme_apply(lv_obj_t *obj);
