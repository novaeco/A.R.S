#pragma once

#include "lvgl.h"

// Screen Identifiers
typedef enum {
  UI_SCREEN_NONE = 0,
  UI_SCREEN_DASHBOARD,
  UI_SCREEN_ANIMALS,
  UI_SCREEN_SETTINGS,
  UI_SCREEN_WIFI,
  UI_SCREEN_DOCUMENTS,
  UI_SCREEN_WEB,
  UI_SCREEN_LOGS,
  UI_SCREEN_ALERTS,
  UI_SCREEN_REPRODUCTION
} ui_screen_t;

// Navigation API
void ui_nav_init(void);
void ui_nav_navigate(ui_screen_t screen, bool anim);
void ui_nav_go_back(void);
