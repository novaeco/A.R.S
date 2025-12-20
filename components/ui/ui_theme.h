#pragma once

#include "lvgl.h"
#include "ui_fonts.h"

// =============================================================================
// Color Palette (Premium Nature Theme)
// =============================================================================
// Primary: Deep Forest Green
#define UI_COLOR_PRIMARY lv_color_hex(0x2E8B57)
// Primary (pressed/focus): Darker Forest
#define UI_COLOR_PRIMARY_DARK lv_color_hex(0x246945)
// Secondary: Earth/Sand
#define UI_COLOR_SECONDARY lv_color_hex(0xD2B48C)
// Secondary (pressed)
#define UI_COLOR_SECONDARY_DARK lv_color_hex(0xB58E62)
// Background: Soft Off-White
#define UI_COLOR_BACKGROUND lv_color_hex(0xF4F4F4)
// Page background (alias of background for clarity)
#define UI_COLOR_PAGE_BG UI_COLOR_BACKGROUND
// Card/Panel: White
#define UI_COLOR_CARD lv_color_hex(0xFFFFFF)
// Surface (alias for elevated containers)
#define UI_COLOR_SURFACE UI_COLOR_CARD
// Text: Dark Charcoal
#define UI_COLOR_TEXT_MAIN lv_color_hex(0x2C3E50)
// Text: Muted Grey
#define UI_COLOR_TEXT_MUTED lv_color_hex(0x7F8C8D)
// Alert: Burnt Orange
#define UI_COLOR_ALERT lv_color_hex(0xE67E22)
// Danger/Error: Deep Red
#define UI_COLOR_DANGER lv_color_hex(0xC0392B)
// Success: Moss Green
#define UI_COLOR_SUCCESS lv_color_hex(0x4CAF50)
// Accent Marker: Vivid Coral for calibration markers
#define UI_COLOR_ACCENT lv_color_hex(0xF54B64)
// Border/Outline: Deep Slate
#define UI_COLOR_OUTLINE lv_color_hex(0x1F2A35)

// =============================================================================
// Typography
// =============================================================================
#define UI_FONT_BODY (&lv_font_montserrat_20)
#define UI_FONT_TITLE (&lv_font_montserrat_20)
#define UI_FONT_SMALL (&lv_font_montserrat_14)

// =============================================================================
// Spacing / Radii / Elevation
// =============================================================================
#define UI_SPACE_XS 4
#define UI_SPACE_SM 8
#define UI_SPACE_MD 12
#define UI_SPACE_LG 16
#define UI_SPACE_XL 24
#define UI_SPACE_XXL 32

#define UI_RADIUS_SM 8
#define UI_RADIUS_MD 12
#define UI_RADIUS_LG 16

#define UI_SHADOW_WIDTH 18
#ifndef UI_SHADOW_MD
#define UI_SHADOW_MD 16
#endif
#define UI_SHADOW_OPA LV_OPA_10

#define UI_HEADER_HEIGHT 64

// =============================================================================
// Styles
// =============================================================================
extern lv_style_t ui_style_screen;
extern lv_style_t ui_style_card;
extern lv_style_t ui_style_title;
extern lv_style_t ui_style_text_body;
extern lv_style_t ui_style_text_muted;
extern lv_style_t ui_style_btn_primary;
extern lv_style_t ui_style_btn_secondary;

// =============================================================================
// API
// =============================================================================
void ui_theme_init(void);
void ui_theme_apply(lv_obj_t *obj);
