#include "screens/ui_dashboard.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_theme.h"
// #include "board.h" // Removed for decoupling
#include "core_service.h" // Audit Fix: Alerts
#include "esp_log.h"
#include "lvgl.h" // Audit Fix: Palette access
#include "ui.h"
#include "net_manager.h"
#include <time.h>

static const char *TAG = "UI_DASH";

// Tile Structure
typedef struct {
  const char *title;
  const char *icon;
  ui_screen_t screen_id;
  bool (*alert_check_cb)(void);
} tile_def_t;

// Alert Check Callbacks (Stubs or mapping to core)
static bool check_alerts(void) {
  char **alerts = NULL;
  size_t count = 0;
  if (core_get_alerts(&alerts, &count) == ESP_OK) { // Audit Fix: Dynamic
    bool has = (count > 0);
    core_free_alert_list(alerts, count);
    return has;
  }
  return false;
}

// Tile configuration
static const tile_def_t tiles[] = {
    {"Fiches Animaux", LV_SYMBOL_LIST, UI_SCREEN_ANIMALS, NULL},
    {"Paramètres", LV_SYMBOL_SETTINGS, UI_SCREEN_SETTINGS, NULL},
    {"Documents", LV_SYMBOL_FILE, UI_SCREEN_DOCUMENTS, NULL},
    {"Test Connexion", LV_SYMBOL_WIFI, UI_SCREEN_WEB, NULL}, // Renamed to Test
    {"Journaux", LV_SYMBOL_EDIT, UI_SCREEN_LOGS, NULL},
    {"Alertes", LV_SYMBOL_WARNING, UI_SCREEN_ALERTS, check_alerts},
};

static lv_obj_t *clock_label = NULL;
static lv_timer_t *clock_timer = NULL;
static lv_obj_t *battery_label = NULL;
static lv_timer_t *battery_timer = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_timer_t *wifi_timer = NULL;

// Forward declarations for timer callbacks
static void clock_timer_cb(lv_timer_t *timer);
static void battery_timer_cb(lv_timer_t *timer);
static void wifi_timer_cb(lv_timer_t *timer);

#include "../ui_screen_manager.h" // ARS: Include Manager

static void dashboard_stop_timers(void) {
  if (clock_timer) {
    lv_timer_del(clock_timer);
    clock_timer = NULL;
  }
  if (battery_timer) {
    lv_timer_del(battery_timer);
    battery_timer = NULL;
  }
  if (wifi_timer) {
    lv_timer_del(wifi_timer);
    wifi_timer = NULL;
  }
}

static void dashboard_start_timers(void) {
  dashboard_stop_timers();

  if (clock_label && lv_obj_is_valid(clock_label)) {
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    clock_timer_cb(clock_timer);
  } else {
    clock_timer = NULL;
  }

  if (battery_label && lv_obj_is_valid(battery_label)) {
    battery_timer = lv_timer_create(battery_timer_cb, 5000, NULL);
    battery_timer_cb(battery_timer);
  } else {
    battery_timer = NULL;
  }

  if (wifi_label && lv_obj_is_valid(wifi_label)) {
    wifi_timer = lv_timer_create(wifi_timer_cb, 2000, NULL);
    wifi_timer_cb(wifi_timer);
  }
}

static void dashboard_delete_event_cb(lv_event_t *e) {
  (void)e;
  dashboard_stop_timers();
  clock_label = NULL;
  battery_label = NULL;
}

void ui_dashboard_cleanup(void) {
  dashboard_stop_timers();
  clock_label = NULL;
  battery_label = NULL;
}

// Event Handler
static void tile_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  ui_screen_t screen_id = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);

  if (code == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Tile Clicked! ScreenID: %d", (int)screen_id);

    // ARS: Show loading for feedback
    ui_show_loading(true);

    // Small delay or just proceed (LVGL will render loading first if we yield?)
    // In blocking logic, we might need a task. Here we just trigger nav.
    // Ideally ui_nav_navigate would turn off loading when done, but since it's
    // sync creation:

    if (screen_id != UI_SCREEN_NONE) {
      ui_nav_navigate(screen_id, true);
      // Loading off after nav (new screen loaded)
      ui_show_loading(false);
    } else {
      ui_show_loading(false);
      ESP_LOGW(TAG, "Navigation target not defined");
    }
  }
}

// Clock Timer
static void clock_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (!clock_label || !lv_obj_is_valid(clock_label)) {
    return;
  }
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);

  lv_label_set_text(clock_label, strftime_buf);
}

// Battery Timer
static void battery_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (!battery_label || !lv_obj_is_valid(battery_label)) {
    return;
  }

  uint8_t percent = 0;
  uint16_t raw = 0;
  esp_err_t err = ui_get_battery_level(&percent, &raw);
  if (err == ESP_OK) {
    lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_FULL " %u%%",
                          (unsigned)percent);
  } else {
    lv_label_set_text(battery_label, LV_SYMBOL_WARNING " ERR");
  }
}

static void wifi_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (!wifi_label || !lv_obj_is_valid(wifi_label)) {
    return;
  }

  net_status_t status = net_get_status();
  wifi_prov_state_t prov = net_manager_get_prov_state();
  const char *icon = status.is_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;

  if (status.is_connected && status.got_ip) {
    lv_label_set_text_fmt(wifi_label, "%s %s", icon, status.ip_addr);
    lv_obj_set_style_text_color(wifi_label, lv_palette_main(LV_PALETTE_GREEN), 0);
  } else if (prov == WIFI_PROV_STATE_NOT_PROVISIONED) {
    lv_label_set_text(wifi_label, LV_SYMBOL_CLOSE " Wi-Fi?");
    lv_obj_set_style_text_color(wifi_label, lv_palette_main(LV_PALETTE_GREY), 0);
  } else if (prov == WIFI_PROV_STATE_CONNECTING) {
    lv_label_set_text(wifi_label, LV_SYMBOL_REFRESH " Connexion...");
    lv_obj_set_style_text_color(wifi_label, lv_palette_main(LV_PALETTE_BLUE), 0);
  } else {
    lv_label_set_text(wifi_label, LV_SYMBOL_CLOSE " Non connecte");
    lv_obj_set_style_text_color(wifi_label, lv_palette_main(LV_PALETTE_RED), 0);
  }
}

// Main Creation
  lv_obj_t *ui_create_dashboard(void) {
    // 1. Create Screen with Theme Background
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(scr, dashboard_delete_event_cb, LV_EVENT_DELETE, NULL);
    ui_theme_apply(scr);

    // 2. Header (shared helper)
    lv_obj_t *header =
        ui_helper_create_header(scr, "Reptiles Assistant", NULL, NULL);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_left(header, UI_SPACE_XL, 0);
    lv_obj_set_style_pad_right(header, UI_SPACE_XL, 0);

    // Status cluster (clock + battery) aligned to the right
  lv_obj_t *status_row = lv_obj_create(header);
  lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(status_row, 0, 0);
  lv_obj_set_style_pad_all(status_row, 0, 0);
  lv_obj_set_size(status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(status_row, UI_SPACE_SM, 0);
  lv_obj_align(status_row, LV_ALIGN_RIGHT_MID, 0, 0);

  clock_label = lv_label_create(status_row);
  lv_label_set_text(clock_label, "00:00");
  lv_obj_add_style(clock_label, &ui_style_title, 0);
  lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);

  wifi_label = lv_label_create(status_row);
  lv_label_set_text(wifi_label, LV_SYMBOL_CLOSE " Wi-Fi");
  lv_obj_add_style(wifi_label, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);

  battery_label = lv_label_create(status_row);
  lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_EMPTY " --%");
  lv_obj_add_style(battery_label, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(battery_label, lv_color_white(), 0);

    // 3. Grid container using flex with responsive sizing
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // 3. Grid
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT + UI_SPACE_MD);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF); // Disable scrollbars
    lv_obj_clear_flag(
        grid, LV_OBJ_FLAG_SCROLLABLE); // Disable scrolling to ensure clicks work

  // Flex Layout for auto-wrap, evenly spaced tiles and generous touch targets
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(grid, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_gap(grid, UI_SPACE_XL, 0);

  // 4. Tiles
  int tile_count = sizeof(tiles) / sizeof(tiles[0]);
  for (int i = 0; i < tile_count; i++) {
    lv_obj_t *btn = lv_button_create(grid);
    lv_obj_set_size(btn, 160, 130);
    lv_obj_add_style(btn, &ui_style_card, 0);
    lv_obj_add_style(btn, &ui_style_btn_secondary, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 1),
                              LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_gap(btn, UI_SPACE_SM, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_LG, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_min_width(btn, 220, 0);
    lv_obj_set_style_min_height(btn, 170, 0);
    lv_obj_set_style_max_width(btn, 320, 0);
    lv_obj_set_style_flex_grow(btn, 1, 0);

    // Override for Alert
    bool has_alert =
        (tiles[i].alert_check_cb) ? tiles[i].alert_check_cb() : false;
    if (has_alert) {
      lv_obj_set_style_bg_color(btn, UI_COLOR_ALERT, 0);
      lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    }

    // Event
    lv_obj_add_event_cb(btn, tile_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)tiles[i].screen_id);

    // Icon
    lv_obj_t *icon = lv_label_create(btn);
    // Audit Fix: Change icon on alert
    if (has_alert) {
      lv_label_set_text(icon, LV_SYMBOL_WARNING);
    } else {
      lv_label_set_text(icon, tiles[i].icon);
    }

    // Use default font or a smaller one if 28 is missing.
    // Scaling transform can be used if larger icon needed, or just use default.
    // lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_transform_scale(
        icon, 384, 0); // Scale up 1.5x (256 base) instead of font change

    if (has_alert) {
      lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    } else {
      lv_obj_set_style_text_color(icon, UI_COLOR_PRIMARY, 0);
    }
    lv_obj_set_style_pad_bottom(icon, UI_SPACE_SM, 0);

    // Label
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, tiles[i].title);
    if (has_alert) {
      lv_obj_set_style_text_color(label, lv_color_white(), 0);
    } else {
      lv_obj_set_style_text_color(label, UI_COLOR_TEXT_MAIN, 0);
    }
  }

  return scr;
}

void ui_dashboard_on_enter(void) { dashboard_start_timers(); }

void ui_dashboard_on_leave(void) {
  dashboard_stop_timers();
  ui_show_loading(false);
}