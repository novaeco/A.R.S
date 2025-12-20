#include "ui_baseline.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_screen_manager.h"
#include "../ui_theme.h"
#include "sd.h"
#include "net_manager.h"
#include "esp_err.h"
#include "ui.h"
#include "lvgl.h"
#include <inttypes.h>
#include <stdbool.h>

static lv_timer_t *s_metrics_timer = NULL;
static lv_obj_t *s_touch_label = NULL;
static lv_obj_t *s_sd_label = NULL;
static lv_obj_t *s_wifi_label = NULL;
static lv_obj_t *s_battery_label = NULL;
static lv_obj_t *s_display_label = NULL;

static lv_color_t tracker_colors[3] = {0};

static void tracker_colors_init(void) {
  static bool initialized = false;
  if (initialized)
    return;
  tracker_colors[0] = UI_COLOR_PRIMARY;
  tracker_colors[1] = UI_COLOR_SUCCESS;
  tracker_colors[2] = UI_COLOR_ALERT;
  initialized = true;
}

static const char *sd_state_to_text(sd_state_t state) {
  switch (state) {
  case SD_STATE_INIT_OK:
    return "SD: montée";
  case SD_STATE_ABSENT:
    return "SD: absente";
  case SD_STATE_INIT_FAIL:
    return "SD: init échouée";
  case SD_STATE_MOUNT_FAIL:
    return "SD: montage échoué";
  default:
    return "SD: --";
  }
}

static const char *wifi_state_to_text(wifi_prov_state_t state) {
  switch (state) {
  case WIFI_PROV_STATE_CONNECTED:
    return "Wi-Fi: connecté";
  case WIFI_PROV_STATE_CONNECTING:
    return "Wi-Fi: connexion...";
  case WIFI_PROV_STATE_CAPTIVE:
    return "Wi-Fi: portail";
  case WIFI_PROV_STATE_WRONG_PASSWORD:
    return "Wi-Fi: mot de passe";
  case WIFI_PROV_STATE_FAILED:
    return "Wi-Fi: échec";
  default:
    return "Wi-Fi: non provisionné";
  }
}

static void update_touch_text(bool pressed, lv_point_t *pt) {
  if (!s_touch_label)
    return;
  if (pressed) {
    lv_label_set_text_fmt(s_touch_label, "Touch: (%" PRId32 ",%" PRId32 ") press",
                          (int32_t)pt->x, (int32_t)pt->y);
  } else {
    lv_label_set_text(s_touch_label, "Touch: relâché");
  }
}

static void tracker_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_get_act();
  lv_point_t pt = {.x = -1, .y = -1};
  if (indev) {
    lv_indev_get_point(indev, &pt);
  }
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    update_touch_text(true, &pt);
  } else if (code == LV_EVENT_RELEASED) {
    update_touch_text(false, &pt);
  }
}

static void metrics_timer_cb(lv_timer_t *timer) {
  (void)timer;
  lv_display_t *disp = lv_display_get_default();
  if (disp && s_display_label) {
    uint32_t hor = lv_display_get_horizontal_resolution(disp);
    uint32_t ver = lv_display_get_vertical_resolution(disp);
    lv_label_set_text_fmt(s_display_label, "Affichage: %lux%lu", (unsigned long)hor,
                          (unsigned long)ver);
  }

  if (s_sd_label) {
    sd_state_t st = sd_get_state();
    lv_label_set_text(s_sd_label, sd_state_to_text(st));
  }

  if (s_wifi_label) {
    wifi_prov_state_t prov = net_manager_get_prov_state();
    lv_label_set_text(s_wifi_label, wifi_state_to_text(prov));
  }

  if (s_battery_label) {
    uint8_t percent = 0;
    uint16_t mv = 0;
    int rc = ui_get_battery_level(&percent, &mv);
    if (rc == ESP_OK) {
      lv_label_set_text_fmt(s_battery_label, "Batterie: %u%% (%umV)", percent,
                            (unsigned int)mv);
    } else {
      lv_label_set_text(s_battery_label, "Batterie: N/A");
    }
  }
}

static void nav_event_cb(lv_event_t *e) {
  ui_screen_t target = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_nav_navigate(target, true);
  }
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_add_style(card, &ui_style_card, 0);
  lv_obj_set_style_pad_all(card, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(card, UI_RADIUS_MD, 0);
  lv_obj_set_style_bg_color(card, UI_COLOR_CARD, 0);
  lv_obj_set_style_shadow_width(card, UI_SHADOW_MD, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, UI_SPACE_SM, 0);

  if (title) {
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_add_style(lbl, &ui_style_title, 0);
  }
  return card;
}

static lv_obj_t *create_status_row(lv_obj_t *parent, const char *icon,
                                   lv_obj_t **label_out) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_all(row, UI_SPACE_SM, 0);
  lv_obj_set_style_radius(row, UI_RADIUS_SM, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(row, UI_SPACE_SM, 0);
  lv_obj_set_style_border_width(row, 0, 0);

  lv_obj_t *icon_lbl = lv_label_create(row);
  lv_label_set_text(icon_lbl, icon);
  lv_obj_add_style(icon_lbl, &ui_style_text_body, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, "--");
  lv_obj_add_style(lbl, &ui_style_text_body, 0);
  if (label_out) {
    *label_out = lbl;
  }
  return row;
}

static void build_primitives(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_all(row, UI_SPACE_SM, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(row, UI_SPACE_SM, 0);

  lv_obj_t *grad = lv_obj_create(row);
  lv_obj_set_size(grad, LV_PCT(60), 120);
  lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_color(grad, UI_COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_grad_color(grad, UI_COLOR_SECONDARY, 0);
  lv_obj_set_style_radius(grad, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(grad, UI_SHADOW_MD, 0);

  lv_obj_t *spinner = lv_spinner_create(row);
  lv_obj_set_size(spinner, 80, 80);
  lv_obj_set_style_arc_color(spinner, UI_COLOR_SUCCESS, 0);
  lv_obj_set_style_arc_width(spinner, 6, 0);
}

static void build_navigation(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(row, UI_SPACE_SM, 0);

  struct {
    const char *text;
    ui_screen_t id;
  } buttons[] = {{"Tableau de bord", UI_SCREEN_DASHBOARD},
                 {"Animaux", UI_SCREEN_ANIMALS},
                 {"Paramètres", UI_SCREEN_SETTINGS},
                 {"Test matériel", UI_SCREEN_HARDWARE_TEST},
                 {"Wi-Fi", UI_SCREEN_WIFI}};

  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_add_style(btn, &ui_style_btn_primary, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, buttons[i].text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)buttons[i].id);
  }
}

lv_obj_t *ui_create_baseline_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "baseline");
  lv_obj_set_style_pad_all(scr, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_gap(scr, UI_SPACE_LG, 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

  ui_helper_create_header(scr, "Baseline 1024x600", NULL, NULL);

  lv_obj_t *status_card = create_card(scr, "Diagnostics rapides");
  lv_obj_set_style_bg_color(status_card, UI_COLOR_SURFACE, 0);
  lv_obj_set_style_pad_gap(status_card, UI_SPACE_SM, 0);

  create_status_row(status_card, LV_SYMBOL_EYE_OPEN, &s_display_label);
  create_status_row(status_card, LV_SYMBOL_IMAGE, &s_touch_label);
  create_status_row(status_card, LV_SYMBOL_SD_CARD, &s_sd_label);
  create_status_row(status_card, LV_SYMBOL_WIFI, &s_wifi_label);
  create_status_row(status_card, LV_SYMBOL_CHARGE, &s_battery_label);

  lv_obj_t *tracker = create_card(scr, "Suivi tactile GT911");
  lv_obj_set_style_pad_gap(tracker, UI_SPACE_SM, 0);
  lv_obj_t *tracker_area = lv_obj_create(tracker);
  lv_obj_set_size(tracker_area, LV_PCT(100), 160);
  tracker_colors_init();
  lv_obj_set_style_bg_color(tracker_area, tracker_colors[lv_tick_get() % 3], 0);
  lv_obj_set_style_bg_grad_color(tracker_area, UI_COLOR_SECONDARY, 0);
  lv_obj_set_style_bg_grad_dir(tracker_area, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_radius(tracker_area, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(tracker_area, UI_SHADOW_MD, 0);
  lv_obj_add_event_cb(tracker_area, tracker_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(tracker_area, LV_OBJ_FLAG_GESTURE_BUBBLE);

  s_touch_label = lv_label_create(tracker);
  lv_label_set_text(s_touch_label, "Touch: attente");
  lv_obj_add_style(s_touch_label, &ui_style_text_body, 0);

  lv_obj_t *primitive_card = create_card(scr, "Validation LVGL");
  build_primitives(primitive_card);

  lv_obj_t *nav_card = create_card(scr, "Navigation rapide");
  build_navigation(nav_card);

  return scr;
}

void ui_baseline_on_enter(void) {
  if (s_metrics_timer) {
    lv_timer_del(s_metrics_timer);
    s_metrics_timer = NULL;
  }
  s_metrics_timer = lv_timer_create(metrics_timer_cb, 1200, NULL);
  metrics_timer_cb(NULL);
}

void ui_baseline_on_leave(void) {
  if (s_metrics_timer) {
    lv_timer_del(s_metrics_timer);
    s_metrics_timer = NULL;
  }
}
