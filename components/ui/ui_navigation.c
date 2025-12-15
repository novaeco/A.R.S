#include "ui_navigation.h"
#include "esp_log.h"
#include "screens/ui_alerts.h"
#include "screens/ui_animals.h"
#include "screens/ui_dashboard.h"
#include "screens/ui_documents.h"
#include "screens/ui_logs.h"
#include "screens/ui_settings.h"
#include "screens/ui_web.h"
#include "screens/ui_wifi.h"
#include "ui.h"
#include "ui_screen_manager.h"
#include <stdbool.h>

static const char *TAG = "UI_NAV";

typedef struct {
  ui_screen_t id;
  lv_obj_t *(*create)(void);
  void (*on_enter)(void);
  void (*on_leave)(void);
  bool reset_stack;
} ui_route_t;

#define MAX_NAV_STACK 10
static ui_screen_t nav_stack[MAX_NAV_STACK];
static int nav_stack_ptr = 0;
static ui_screen_t current_screen_id = UI_SCREEN_NONE;

static const ui_route_t routes[] = {
    {UI_SCREEN_DASHBOARD, ui_create_dashboard, ui_dashboard_on_enter,
     ui_dashboard_on_leave, true},
    {UI_SCREEN_ANIMALS, ui_create_animal_list_screen, NULL, NULL, false},
    {UI_SCREEN_SETTINGS, ui_create_settings_screen, NULL, NULL, false},
    {UI_SCREEN_WIFI, ui_create_screen_wifi, ui_wifi_on_enter, ui_wifi_on_leave,
     false},
    {UI_SCREEN_DOCUMENTS, ui_create_documents_screen, NULL, NULL, false},
    {UI_SCREEN_WEB, ui_create_web_screen, NULL, NULL, false},
    {UI_SCREEN_LOGS, ui_create_logs_screen, NULL, NULL, false},
    {UI_SCREEN_ALERTS, ui_create_alerts_screen, NULL, NULL, false},
};

static const ui_route_t *ui_nav_find_route(ui_screen_t id) {
  for (size_t i = 0; i < (sizeof(routes) / sizeof(routes[0])); i++) {
    if (routes[i].id == id) {
      return &routes[i];
    }
  }
  return NULL;
}

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

void ui_nav_init(void) {
  nav_stack_ptr = 0;
  current_screen_id = UI_SCREEN_NONE;
}

void ui_nav_navigate(ui_screen_t screen, bool anim) {
  const ui_route_t *route = ui_nav_find_route(screen);
  if (!route || !route->create) {
    ESP_LOGW(TAG, "Unknown screen %d", (int)screen);
    return;
  }

  const ui_route_t *current_route = ui_nav_find_route(current_screen_id);

  if (route->reset_stack) {
    nav_stack_ptr = 0;
  } else if (screen != current_screen_id && current_screen_id != UI_SCREEN_NONE) {
    ui_nav_push(current_screen_id);
  }

  if (current_route && current_route->on_leave) {
    current_route->on_leave();
  }

  lv_obj_t *scr = route->create();
  if (!scr) {
    ESP_LOGE(TAG, "Screen creation failed for %d", (int)screen);
    return;
  }

  lv_scr_load_anim_t anim_type =
      anim ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_NONE;
  ui_switch_screen(scr, anim_type);

  current_screen_id = screen;

  if (route->on_enter) {
    route->on_enter();
  }
}

void ui_nav_go_back(void) {
  ui_screen_t prev = ui_nav_pop();
  ui_nav_navigate(prev, true);
}
