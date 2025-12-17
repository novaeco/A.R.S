#include "ui_navigation.h"
#include "esp_log.h"
#include "screens/ui_alerts.h"
#include "screens/ui_animals.h"
#include "screens/ui_dashboard.h"
#include "screens/ui_documents.h"
#include "screens/ui_logs.h"
#include "screens/ui_settings.h"
#include "screens/ui_animal_details.h"
#include "screens/ui_animal_form.h"
#include "screens/ui_reproduction.h"
#include "screens/ui_web.h"
#include "screens/ui_wifi.h"
#include "ui.h"
#include "ui_screen_manager.h"
#include <stdbool.h>

static const char *TAG = "UI_NAV";

typedef lv_obj_t *(*ui_screen_create_cb_t)(void *ctx);
typedef void (*ui_screen_simple_cb_t)(void);

typedef struct {
  ui_screen_t id;
  ui_screen_create_cb_t create;
  ui_screen_simple_cb_t on_enter;
  ui_screen_simple_cb_t on_leave;
  bool reset_stack;
} ui_route_t;

#define MAX_NAV_STACK 10
static ui_screen_t nav_stack[MAX_NAV_STACK];
static int nav_stack_ptr = 0;
static ui_screen_t current_screen_id = UI_SCREEN_NONE;

static lv_obj_t *create_dashboard(void *ctx) {
  (void)ctx;
  return ui_create_dashboard();
}

static lv_obj_t *create_animals(void *ctx) {
  (void)ctx;
  return ui_create_animal_list_screen();
}

static lv_obj_t *create_settings(void *ctx) {
  (void)ctx;
  return ui_create_settings_screen();
}

static lv_obj_t *create_wifi(void *ctx) {
  (void)ctx;
  return ui_create_screen_wifi();
}

static lv_obj_t *create_documents(void *ctx) {
  (void)ctx;
  return ui_create_documents_screen();
}

static lv_obj_t *create_web(void *ctx) {
  (void)ctx;
  return ui_create_web_screen();
}

static lv_obj_t *create_logs(void *ctx) {
  (void)ctx;
  return ui_create_logs_screen();
}

static lv_obj_t *create_alerts(void *ctx) {
  (void)ctx;
  return ui_create_alerts_screen();
}

static lv_obj_t *create_animal_details(void *ctx) {
  return ui_create_animal_details_screen((const char *)ctx);
}

static lv_obj_t *create_animal_form(void *ctx) {
  return ui_create_animal_form_screen((const char *)ctx);
}

static lv_obj_t *create_reproduction(void *ctx) {
  return ui_create_reproduction_screen((const char *)ctx);
}

static const ui_route_t routes[] = {
    {UI_SCREEN_DASHBOARD, create_dashboard, ui_dashboard_on_enter,
     ui_dashboard_on_leave, true},
    {UI_SCREEN_ANIMALS, create_animals, NULL, NULL, false},
    {UI_SCREEN_SETTINGS, create_settings, NULL, NULL, false},
    {UI_SCREEN_WIFI, create_wifi, ui_wifi_on_enter, ui_wifi_on_leave, false},
    {UI_SCREEN_DOCUMENTS, create_documents, NULL, NULL, false},
    {UI_SCREEN_WEB, create_web, NULL, NULL, false},
    {UI_SCREEN_LOGS, create_logs, NULL, NULL, false},
    {UI_SCREEN_ALERTS, create_alerts, NULL, NULL, false},
    {UI_SCREEN_ANIMAL_DETAILS, create_animal_details, NULL, NULL, false},
    {UI_SCREEN_ANIMAL_FORM, create_animal_form, NULL, NULL, false},
    {UI_SCREEN_REPRODUCTION, create_reproduction, NULL, NULL, false},
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

void ui_nav_navigate_ctx(ui_screen_t screen, void *ctx, bool anim) {
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

  lv_obj_t *scr = route->create(ctx);
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

void ui_nav_navigate(ui_screen_t screen, bool anim) {
  ui_nav_navigate_ctx(screen, NULL, anim);
}

void ui_nav_go_back(void) {
  ui_screen_t prev = ui_nav_pop();
  ui_nav_navigate(prev, true);
}
