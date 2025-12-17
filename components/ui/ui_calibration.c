#include "ui_calibration.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "touch.h"
#include "touch_orient.h"
#include "ui_helpers.h"
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include "ui_wizard.h"
#include <inttypes.h>

static const char *TAG = "ui_calibration";

static lv_timer_t *s_touch_dbg_timer = NULL;
static lv_obj_t *s_touch_dbg_label = NULL;
static lv_obj_t *s_orientation_label = NULL;
static lv_obj_t *s_switch_swap = NULL;
static lv_obj_t *s_switch_mir_x = NULL;
static lv_obj_t *s_switch_mir_y = NULL;
static touch_orient_config_t s_current_cfg = {0};

typedef enum {
  CAL_FLAG_SWAP = 0,
  CAL_FLAG_MIRROR_X,
  CAL_FLAG_MIRROR_Y,
} cal_flag_id_t;

static void stop_touch_debug_timer(void) {
  if (s_touch_dbg_timer) {
    lv_timer_del(s_touch_dbg_timer);
    s_touch_dbg_timer = NULL;
  }
}

static void apply_identity_calibration(void) {
  esp_lcd_touch_handle_t tp = app_board_get_touch_handle();
  if (!tp) {
    ESP_LOGW(TAG, "Touch handle not available; cannot apply calibration");
    return;
  }

  // Apply orientation flags first
  esp_err_t err = touch_orient_apply(tp, &s_current_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to apply orientation: %s", esp_err_to_name(err));
  }

  // Ensure no stale scale/offset remain; we use identity by default.
  ars_touch_calibration_t identity = {
      .scale_x = 1.0f,
      .scale_y = 1.0f,
      .offset_x = 0,
      .offset_y = 0,
  };
  ars_touch_set_calibration(tp, &identity);
  ESP_LOGI(TAG, "Calibration applied (swap=%d mir_x=%d mir_y=%d)",
           s_current_cfg.swap_xy, s_current_cfg.mirror_x, s_current_cfg.mirror_y);
}

static void update_orientation_label(void) {
  if (!s_orientation_label)
    return;

  lv_label_set_text_fmt(s_orientation_label,
                        "Orientation actuelle:\nSwap XY: %s\nMiroir X: %s\nMiroir Y: %s",
                        s_current_cfg.swap_xy ? "ON" : "OFF",
                        s_current_cfg.mirror_x ? "ON" : "OFF",
                        s_current_cfg.mirror_y ? "ON" : "OFF");
}

static void sync_switch_states(void) {
  if (s_switch_swap) {
    if (s_current_cfg.swap_xy) {
      lv_obj_add_state(s_switch_swap, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_swap, LV_STATE_CHECKED);
    }
  }
  if (s_switch_mir_x) {
    if (s_current_cfg.mirror_x) {
      lv_obj_add_state(s_switch_mir_x, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_mir_x, LV_STATE_CHECKED);
    }
  }
  if (s_switch_mir_y) {
    if (s_current_cfg.mirror_y) {
      lv_obj_add_state(s_switch_mir_y, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_mir_y, LV_STATE_CHECKED);
    }
  }
  update_orientation_label();
}

static void touch_debug_timer_cb(lv_timer_t *timer) {
  if (!timer) {
    return;
  }
  lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (!label || !lv_obj_is_valid(label)) {
    return;
  }
  ars_touch_debug_info_t info = {0};
  if (ars_touch_debug_get(&info) != ESP_OK) {
    return;
  }

  char buf[128];
  lv_snprintf(buf, sizeof(buf),
              "raw:%d,%d xy:%d,%d irq:%" PRIu32 " empty:%" PRIu32
              " err:%" PRIu32 "%s",
              info.raw_x, info.raw_y, info.x, info.y, info.irq_total,
              info.empty_irqs, info.i2c_errors, info.polling ? " poll" : "");
  lv_label_set_text(label, buf);
}

static void on_toggle_event(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    return;

  lv_obj_t *target = lv_event_get_target(e);
  cal_flag_id_t flag = (cal_flag_id_t)(uintptr_t)lv_event_get_user_data(e);
  bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);

  switch (flag) {
  case CAL_FLAG_SWAP:
    s_current_cfg.swap_xy = enabled;
    break;
  case CAL_FLAG_MIRROR_X:
    s_current_cfg.mirror_x = enabled;
    break;
  case CAL_FLAG_MIRROR_Y:
    s_current_cfg.mirror_y = enabled;
    break;
  }

  apply_identity_calibration();
  sync_switch_states();
}

static void reset_defaults_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  touch_orient_get_defaults(&s_current_cfg);
  apply_identity_calibration();
  sync_switch_states();
  ui_show_toast("Calibration par d\u00e9faut appliqu\u00e9e", UI_TOAST_INFO);
}

static void save_and_finish_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  esp_err_t err = touch_orient_save(&s_current_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save calibration: %s", esp_err_to_name(err));
    ui_show_error("Echec enregistrement calibration");
    return;
  }

  ESP_LOGI(TAG, "Calibration saved to NVS");

  err = ui_wizard_mark_setup_done();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to persist setup_done flag: %s", esp_err_to_name(err));
  }

  stop_touch_debug_timer();
  ui_wizard_complete_from_calibration();
}

static lv_obj_t *create_toggle_row(lv_obj_t *parent, const char *label_txt,
                                   cal_flag_id_t flag, bool initial_state,
                                   lv_obj_t **out_switch) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(row, UI_RADIUS_MD, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(row, LV_PCT(100));

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_txt);
  lv_obj_add_style(lbl, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

  lv_obj_t *sw = lv_switch_create(row);
  if (initial_state) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, on_toggle_event, LV_EVENT_VALUE_CHANGED,
                      (void *)(uintptr_t)flag);
  if (out_switch) {
    *out_switch = sw;
  }
  return row;
}

static void build_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  ui_theme_apply(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *header =
      ui_helper_create_header(scr, "Calibration tactile", NULL, NULL /* back */);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *body = lv_obj_create(scr);
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT + UI_SPACE_MD);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_gap(body, UI_SPACE_LG, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(body);
  lv_label_set_text(title, "Ajustez l'orientation du tactile");
  lv_obj_add_style(title, &ui_style_title, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);

  lv_obj_t *desc = lv_label_create(body);
  lv_label_set_text(desc,
                    "V\u00e9rifiez que le doigt suit bien les croix rouges."
                    " Activez Swap/Miroir si le pointeur est invers\u00e9,"
                    " puis validez pour poursuivre.");
  lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(desc, LV_PCT(90));
  lv_obj_add_style(desc, &ui_style_text_body, 0);
  lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *toggles = lv_obj_create(body);
  lv_obj_set_style_bg_opa(toggles, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(toggles, 0, 0);
  lv_obj_set_style_pad_gap(toggles, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(toggles, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(toggles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(toggles, LV_PCT(90));

  create_toggle_row(toggles, "Swap X/Y", CAL_FLAG_SWAP, s_current_cfg.swap_xy,
                    &s_switch_swap);
  create_toggle_row(toggles, "Miroir X", CAL_FLAG_MIRROR_X,
                    s_current_cfg.mirror_x, &s_switch_mir_x);
  create_toggle_row(toggles, "Miroir Y", CAL_FLAG_MIRROR_Y,
                    s_current_cfg.mirror_y, &s_switch_mir_y);

  lv_obj_t *card = lv_obj_create(body);
  lv_obj_set_width(card, LV_PCT(90));
  lv_obj_set_style_pad_all(card, UI_SPACE_MD, 0);
  lv_obj_set_style_radius(card, UI_RADIUS_MD, 0);
  lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, UI_SHADOW_MD, 0);
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, UI_SPACE_SM, 0);

  s_orientation_label = lv_label_create(card);
  lv_obj_add_style(s_orientation_label, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(s_orientation_label, lv_color_white(), 0);
  update_orientation_label();

  lv_obj_t *coord_hint = lv_label_create(card);
  lv_label_set_text(coord_hint,
                    "Les croix rouges sont plac\u00e9es dans les 4 coins et au centre."
                    " Le texte ci-dessous affiche les coordonn\u00e9es brutes"
                    " lues depuis le GT911.");
  lv_label_set_long_mode(coord_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(coord_hint, LV_PCT(100));
  lv_obj_add_style(coord_hint, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(coord_hint, lv_color_white(), 0);

  s_touch_dbg_label = lv_label_create(card);
  lv_obj_set_style_bg_opa(s_touch_dbg_label, LV_OPA_40, 0);
  lv_obj_set_style_bg_color(s_touch_dbg_label, lv_color_black(), 0);
  lv_obj_set_style_text_color(s_touch_dbg_label, lv_color_white(), 0);
  lv_obj_set_style_pad_all(s_touch_dbg_label, UI_SPACE_SM, 0);
  lv_label_set_text(s_touch_dbg_label, "touch dbg");

  lv_obj_t *actions = lv_obj_create(body);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_pad_gap(actions, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_reset = lv_button_create(actions);
  lv_obj_add_style(btn_reset, &ui_style_btn_secondary, 0);
  lv_obj_set_style_min_width(btn_reset, 180, 0);
  lv_obj_set_style_min_height(btn_reset, 52, 0);
  lv_obj_add_event_cb(btn_reset, reset_defaults_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_reset), "Restaurer d\u00e9fauts");

  lv_obj_t *btn_validate = lv_button_create(actions);
  lv_obj_add_style(btn_validate, &ui_style_btn_primary, 0);
  lv_obj_set_style_min_width(btn_validate, 180, 0);
  lv_obj_set_style_min_height(btn_validate, 52, 0);
  lv_obj_add_event_cb(btn_validate, save_and_finish_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_validate), "Valider");

  // Visual markers to verify orientation (non-interactive)
  lv_obj_t *overlay = lv_obj_create(scr);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);

  const lv_align_t aligns[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
                               LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT,
                               LV_ALIGN_CENTER};
  for (int i = 0; i < 5; ++i) {
    lv_obj_t *marker = lv_obj_create(overlay);
    lv_obj_set_size(marker, 26, 26);
    lv_obj_set_style_bg_color(marker, LV_COLOR_MAKE(0xF5, 0x4B, 0x64), 0);
    lv_obj_set_style_radius(marker, 6, 0);
    lv_obj_set_style_border_color(marker, lv_color_white(), 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(marker, aligns[i], (i == 4) ? 0 : 8, (i == 4) ? 0 : 8);
  }

  if (s_touch_dbg_timer) {
    lv_timer_del(s_touch_dbg_timer);
  }
  s_touch_dbg_timer = lv_timer_create(touch_debug_timer_cb, 200, s_touch_dbg_label);

  ui_switch_screen(scr, LV_SCR_LOAD_ANIM_NONE);
}

void ui_calibration_apply(const touch_orient_config_t *cfg) {
  if (cfg) {
    s_current_cfg = *cfg;
  } else {
    touch_orient_get_defaults(&s_current_cfg);
  }
  apply_identity_calibration();
  sync_switch_states();
}

bool ui_calibration_check_and_start(void) {
  touch_orient_config_t cfg;
  esp_err_t err = touch_orient_load(&cfg);
  if (err == ESP_OK) {
    ui_calibration_apply(&cfg);
    return false;
  }

  ESP_LOGW(TAG, "Calibration missing or invalid (%s); starting wizard",
           esp_err_to_name(err));
  ui_calibration_start();
  return true;
}

void ui_calibration_start(void) {
  ESP_LOGI(TAG, "Starting Calibration UI");

  if (touch_orient_load(&s_current_cfg) != ESP_OK) {
    touch_orient_get_defaults(&s_current_cfg);
  }
  apply_identity_calibration();
  build_screen();
}
