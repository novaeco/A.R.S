#include "ui_navigation.h"
#include "esp_log.h"
#include "screens/ui_dashboard.h"
#include "screens/ui_wifi.h"
#include "ui.h"

// static const char *TAG = "UI_NAV"; // Unused

// Include other screens as needed (assuming headers exist or will be cleaned
// up) For now declaring externs or assuming headers match standard naming if
// not included in ui.h
// Ideally ui.h should expose them or we include individual headers.
#include "screens/ui_alerts.h"
#include "screens/ui_animals.h"
#include "screens/ui_documents.h"
#include "screens/ui_logs.h"
#include "screens/ui_settings.h"
#include "screens/ui_web.h"

// Helper: Forward declaration if headers are missing
// void ui_create_animal_list_screen(void); // In ui_animals.h
// void ui_create_settings_screen(void);    // In ui_settings.h

#define MAX_NAV_STACK 10
static ui_screen_t nav_stack[MAX_NAV_STACK];
static int nav_stack_ptr = 0;

static void ui_nav_push(ui_screen_t screen) {
  if (nav_stack_ptr < MAX_NAV_STACK) {
    nav_stack[nav_stack_ptr++] = screen;
  }
}

static ui_screen_t ui_nav_pop(void) {
  if (nav_stack_ptr > 0) {
    return nav_stack[--nav_stack_ptr];
  }
  return UI_SCREEN_DASHBOARD;
}

void ui_nav_init(void) { nav_stack_ptr = 0; }

void ui_nav_navigate(ui_screen_t screen, bool anim) {
  // 1. Get current screen object BEFORE creating the new one
  // lv_obj_t *old_scr = lv_scr_act(); // Unused now

  // 2. Manage Stack (basic)
  static ui_screen_t current_screen_id = UI_SCREEN_DASHBOARD;
  if (screen == UI_SCREEN_DASHBOARD) {
    nav_stack_ptr = 0; // Reset stack on dashboard
  } else if (screen != current_screen_id) {
    ui_nav_push(current_screen_id);
  }
  current_screen_id = screen;

  // 3. Create and Load new screen
  // NOTE: Each ui_create_... function MUST call lv_scr_load(new_scr) at the
  // end.
  switch (screen) {
  case UI_SCREEN_DASHBOARD:
    ui_create_dashboard();
    break;
  case UI_SCREEN_ANIMALS:
    ESP_LOGI("UI_NAV", "Creating Animals Screen");
    ui_create_animal_list_screen();
    break;
  case UI_SCREEN_SETTINGS:
    ui_create_settings_screen();
    break;
  case UI_SCREEN_WIFI:
    ui_create_screen_wifi();
    break;
  case UI_SCREEN_DOCUMENTS:
    ui_create_documents_screen();
    break;
  case UI_SCREEN_WEB:
    ui_create_web_screen();
    break;
  case UI_SCREEN_LOGS:
    ui_create_logs_screen();
    break;
  case UI_SCREEN_ALERTS:
    ui_create_alerts_screen();
    break;
  /*
  case UI_SCREEN_REPRODUCTION:
      ui_create_reproduction_screen();
      break;
  */
  default:
    break;
  }

  // 4. Old screen deletion is handled by lv_scr_load_anim (auto_del=true) in
  // ui_switch_screen or by the default loader if not using manager everywhere
  // yet. For mixed usage, we rely on the transition function to handle cleanup.
}

void ui_nav_go_back(void) {
  ui_screen_t prev = ui_nav_pop();
  ui_nav_navigate(prev, true);
}
