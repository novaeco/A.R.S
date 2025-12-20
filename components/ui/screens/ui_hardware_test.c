#include "ui_hardware_test.h"
#include "../ui_helpers.h"
#include "../ui_navigation.h"
#include "../ui_screen_manager.h"
#include "../ui_theme.h"
#include "board.h"
#include "sd.h"
#include "net_manager.h"
#include "lvgl.h"

static lv_timer_t *s_status_timer = NULL;
static lv_obj_t *s_sd_label = NULL;
static lv_obj_t *s_wifi_label = NULL;
static lv_obj_t *s_backlight_label = NULL;
static lv_obj_t *s_pattern_area = NULL;

static const lv_color_t pattern_colors[] = {UI_COLOR_PRIMARY, UI_COLOR_SUCCESS,
                                            UI_COLOR_DANGER};

static void update_backlight_label(uint8_t percent) {
  if (!s_backlight_label)
    return;
  lv_label_set_text_fmt(s_backlight_label, "Rétroéclairage: %u%%", percent);
}

static void backlight_event_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (!slider || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    return;
  uint8_t val = (uint8_t)lv_slider_get_value(slider);
  board_set_backlight_percent(val);
  update_backlight_label(val);
}

static void pattern_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_pattern_area)
    return;
  static size_t idx = 0;
  idx = (idx + 1) % (sizeof(pattern_colors) / sizeof(pattern_colors[0]));
  lv_obj_set_style_bg_color(s_pattern_area, pattern_colors[idx], 0);
  lv_obj_set_style_bg_grad_color(s_pattern_area, UI_COLOR_SECONDARY, 0);
}

static void refresh_status_labels(void) {
  if (s_sd_label) {
    sd_state_t st = sd_get_state();
    switch (st) {
    case SD_STATE_MOUNTED:
      lv_label_set_text(s_sd_label, "SD: montée");
      break;
    case SD_STATE_INIT_FAIL:
      lv_label_set_text(s_sd_label, "SD: init échouée");
      ui_show_error("Init SD échouée");
      break;
    case SD_STATE_MOUNT_FAIL:
      lv_label_set_text(s_sd_label, "SD: montage échoué");
      break;
    default:
      lv_label_set_text(s_sd_label, "SD: absente");
      break;
    }
  }

  if (s_wifi_label) {
    wifi_prov_state_t prov = net_manager_get_prov_state();
    switch (prov) {
    case WIFI_PROV_STATE_CONNECTED:
      lv_label_set_text(s_wifi_label, "Wi-Fi: connecté");
      break;
    case WIFI_PROV_STATE_CONNECTING:
      lv_label_set_text(s_wifi_label, "Wi-Fi: connexion");
      break;
    case WIFI_PROV_STATE_PROVISIONING:
    case WIFI_PROV_STATE_PROV_DONE:
      lv_label_set_text(s_wifi_label, "Wi-Fi: provisionné");
      break;
    case WIFI_PROV_STATE_FAILED:
      lv_label_set_text(s_wifi_label, "Wi-Fi: échec");
      break;
    default:
      lv_label_set_text(s_wifi_label, "Wi-Fi: non provisionné");
      break;
    }
  }
}

static void status_timer_cb(lv_timer_t *timer) {
  (void)timer;
  refresh_status_labels();
}

static void back_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_nav_navigate(UI_SCREEN_BASELINE, true);
  }
}

lv_obj_t *ui_create_hardware_test_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_screen_claim_with_theme(scr, "hardware_test");
  lv_obj_set_style_pad_all(scr, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_gap(scr, UI_SPACE_LG, 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

  ui_helper_create_header(scr, "Test matériel", back_cb, "Baseline");

  lv_obj_t *status_card = lv_obj_create(scr);
  lv_obj_set_width(status_card, LV_PCT(100));
  lv_obj_add_style(status_card, &ui_style_card, 0);
  lv_obj_set_style_pad_all(status_card, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(status_card, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(status_card, UI_SHADOW_MD, 0);
  lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(status_card, UI_SPACE_SM, 0);

  lv_obj_t *row = lv_obj_create(status_card);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_all(row, UI_SPACE_SM, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(row, UI_SPACE_SM, 0);

  lv_obj_t *sd_icon = lv_label_create(row);
  lv_label_set_text(sd_icon, LV_SYMBOL_SD_CARD);
  s_sd_label = lv_label_create(row);
  lv_label_set_text(s_sd_label, "SD: --");
  lv_obj_add_style(s_sd_label, &ui_style_text_body, 0);

  lv_obj_t *wifi_icon = lv_label_create(row);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  s_wifi_label = lv_label_create(row);
  lv_label_set_text(s_wifi_label, "Wi-Fi: --");
  lv_obj_add_style(s_wifi_label, &ui_style_text_body, 0);

  lv_obj_t *pattern_card = lv_obj_create(scr);
  lv_obj_set_width(pattern_card, LV_PCT(100));
  lv_obj_add_style(pattern_card, &ui_style_card, 0);
  lv_obj_set_style_pad_all(pattern_card, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(pattern_card, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(pattern_card, UI_SHADOW_MD, 0);
  lv_obj_set_flex_flow(pattern_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(pattern_card, UI_SPACE_SM, 0);

  lv_obj_t *pattern_title = lv_label_create(pattern_card);
  lv_label_set_text(pattern_title, "Rendu couleur / tearing");
  lv_obj_add_style(pattern_title, &ui_style_title, 0);

  s_pattern_area = lv_obj_create(pattern_card);
  lv_obj_set_size(s_pattern_area, LV_PCT(100), 160);
  lv_obj_set_style_bg_color(s_pattern_area, pattern_colors[0], 0);
  lv_obj_set_style_bg_grad_color(s_pattern_area, UI_COLOR_SECONDARY, 0);
  lv_obj_set_style_bg_grad_dir(s_pattern_area, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(s_pattern_area, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(s_pattern_area, UI_SHADOW_MD, 0);
  lv_obj_add_flag(s_pattern_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(s_pattern_area, pattern_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *backlight_card = lv_obj_create(scr);
  lv_obj_set_width(backlight_card, LV_PCT(100));
  lv_obj_add_style(backlight_card, &ui_style_card, 0);
  lv_obj_set_style_pad_all(backlight_card, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(backlight_card, UI_RADIUS_MD, 0);
  lv_obj_set_style_shadow_width(backlight_card, UI_SHADOW_MD, 0);
  lv_obj_set_flex_flow(backlight_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(backlight_card, UI_SPACE_SM, 0);

  lv_obj_t *backlight_title = lv_label_create(backlight_card);
  lv_label_set_text(backlight_title, "Rétroéclairage");
  lv_obj_add_style(backlight_title, &ui_style_title, 0);

  lv_obj_t *slider = lv_slider_create(backlight_card);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 80, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, backlight_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  s_backlight_label = lv_label_create(backlight_card);
  lv_obj_add_style(s_backlight_label, &ui_style_text_body, 0);
  update_backlight_label(80);

  return scr;
}

void ui_hardware_test_on_enter(void) {
  if (s_status_timer) {
    lv_timer_del(s_status_timer);
    s_status_timer = NULL;
  }
  s_status_timer = lv_timer_create(status_timer_cb, 1500, NULL);
  refresh_status_labels();
}

void ui_hardware_test_on_leave(void) {
  if (s_status_timer) {
    lv_timer_del(s_status_timer);
    s_status_timer = NULL;
  }
  s_sd_label = NULL;
  s_wifi_label = NULL;
  s_backlight_label = NULL;
  s_pattern_area = NULL;
}
