#include "ui_calibration.h"
#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "touch.h"
#include "touch_transform.h"
#include "ui_helpers.h"
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include "ui_wizard.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>

static const char *TAG = "ui_calibration";

static lv_timer_t *s_touch_dbg_timer = NULL;
static lv_obj_t *s_touch_dbg_label = NULL;
static lv_obj_t *s_orientation_label = NULL;
static lv_obj_t *s_cal_progress_label = NULL;
static lv_obj_t *s_switch_swap = NULL;
static lv_obj_t *s_switch_mir_x = NULL;
static lv_obj_t *s_switch_mir_y = NULL;
static lv_obj_t *s_capture_layer = NULL;
static touch_transform_record_t s_current_record = {0};
static touch_transform_metrics_t s_last_metrics = {0};
static lv_timer_t *s_auto_timer = NULL;
static struct {
  uint32_t start_tick;
  lv_point_t origin;
  lv_point_t delta;
} s_auto_probe;

#define CAL_POINT_COUNT 5
typedef struct {
  lv_point_t target;
  lv_point_t measured_raw;
  bool captured;
  lv_obj_t *marker;
} cal_point_t;

static cal_point_t s_cal_points[CAL_POINT_COUNT];
static uint8_t s_active_cal_point = 0;
static bool s_is_collecting = false;

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

static void stop_auto_timer(void) {
  if (s_auto_timer) {
    lv_timer_del(s_auto_timer);
    s_auto_timer = NULL;
  }
}

static void apply_config_to_driver(bool reset_scale) {
  esp_lcd_touch_handle_t tp = app_board_get_touch_handle();
  if (!tp) {
    ESP_LOGW(TAG, "Touch handle not available; cannot apply calibration");
    return;
  }

  touch_transform_t tf = s_current_record.transform;
  if (reset_scale) {
    touch_transform_identity(&tf);
    tf.swap_xy = s_current_record.transform.swap_xy;
    tf.mirror_x = s_current_record.transform.mirror_x;
    tf.mirror_y = s_current_record.transform.mirror_y;
  }
  touch_transform_set_active(&tf);
}

static void update_orientation_label(void) {
  if (!s_orientation_label)
    return;

  lv_label_set_text_fmt(
      s_orientation_label,
      "Orientation actuelle :\nSwap XY: %s\nMiroir X: %s\nMiroir Y: %s"
      "\nAffine: [%.3f %.3f %.1f; %.3f %.3f %.1f]",
      s_current_record.transform.swap_xy ? "ON" : "OFF",
      s_current_record.transform.mirror_x ? "ON" : "OFF",
      s_current_record.transform.mirror_y ? "ON" : "OFF",
      (double)s_current_record.transform.a11,
      (double)s_current_record.transform.a12,
      (double)s_current_record.transform.a13,
      (double)s_current_record.transform.a21,
      (double)s_current_record.transform.a22,
      (double)s_current_record.transform.a23);
}

static void sync_switch_states(void) {
  if (s_switch_swap) {
    if (s_current_record.transform.swap_xy) {
      lv_obj_add_state(s_switch_swap, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_swap, LV_STATE_CHECKED);
    }
  }
  if (s_switch_mir_x) {
    if (s_current_record.transform.mirror_x) {
      lv_obj_add_state(s_switch_mir_x, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_mir_x, LV_STATE_CHECKED);
    }
  }
  if (s_switch_mir_y) {
    if (s_current_record.transform.mirror_y) {
      lv_obj_add_state(s_switch_mir_y, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_switch_mir_y, LV_STATE_CHECKED);
    }
  }
  update_orientation_label();
}

static void set_progress_text(const char *text_fmt, ...) {
  if (!s_cal_progress_label)
    return;
  va_list args;
  va_start(args, text_fmt);
  lv_label_set_text_vfmt(s_cal_progress_label, text_fmt, args);
  va_end(args);
}

static void auto_timer_cb(lv_timer_t *t) {
  (void)t;
  touch_sample_raw_t sample =
      touch_transform_sample_raw_oriented(app_board_get_touch_handle(), true);
  s_auto_probe.delta.x = sample.raw_x - s_auto_probe.origin.x;
  s_auto_probe.delta.y = sample.raw_y - s_auto_probe.origin.y;

  if (lv_tick_elaps(s_auto_probe.start_tick) > 800) {
    stop_auto_timer();
    bool swap = abs(s_auto_probe.delta.x) < abs(s_auto_probe.delta.y);
    s_current_record.transform.swap_xy = swap;
    s_current_record.transform.mirror_x = s_auto_probe.delta.x < 0;
    s_current_record.transform.mirror_y = s_auto_probe.delta.y < 0;
    apply_config_to_driver(false);
    sync_switch_states();
    set_progress_text("Auto orientation swap=%s mirX=%s mirY=%s",
                      swap ? "oui" : "non",
                      s_current_record.transform.mirror_x ? "oui" : "non",
                      s_current_record.transform.mirror_y ? "oui" : "non");
    ui_show_toast("Orientation deduite, verifiez sur l'ecran",
                  UI_TOAST_INFO);
  }
}

static void highlight_active_marker(void) {
  for (int i = 0; i < CAL_POINT_COUNT; ++i) {
    if (!s_cal_points[i].marker)
      continue;
    lv_color_t color = (i == s_active_cal_point) ? UI_COLOR_ACCENT
                                                 : UI_COLOR_SECONDARY;
    lv_obj_set_style_bg_color(s_cal_points[i].marker, color, 0);
    lv_obj_set_style_border_color(s_cal_points[i].marker, lv_color_white(), 0);
  }
}

static void stop_capture_layer(void) {
  s_is_collecting = false;
  if (s_capture_layer) {
    lv_obj_clear_flag(s_capture_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_capture_layer, LV_OBJ_FLAG_HIDDEN);
  }
  stop_auto_timer();
}

static void reset_cal_points(lv_obj_t *overlay) {
  lv_display_t *disp = lv_obj_get_display(overlay);
  lv_coord_t w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t h = lv_display_get_vertical_resolution(disp);
  const lv_coord_t margin = 60;

  const lv_point_t targets[CAL_POINT_COUNT] = {
      {.x = margin, .y = margin},
      {.x = w - margin, .y = margin},
      {.x = w - margin, .y = h - margin},
      {.x = margin, .y = h - margin},
      {.x = w / 2, .y = h / 2},
  };

    for (int i = 0; i < CAL_POINT_COUNT; ++i) {
      s_cal_points[i].target = targets[i];
      s_cal_points[i].measured_raw.x = 0;
      s_cal_points[i].measured_raw.y = 0;
      s_cal_points[i].captured = false;
    if (s_cal_points[i].marker) {
      lv_obj_del(s_cal_points[i].marker);
      s_cal_points[i].marker = NULL;
    }
    lv_obj_t *marker = lv_obj_create(overlay);
    lv_obj_set_size(marker, 26, 26);
    lv_obj_set_style_radius(marker, 6, 0);
    lv_obj_set_style_border_color(marker, lv_color_white(), 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(marker, targets[i].x - 13, targets[i].y - 13);
    s_cal_points[i].marker = marker;
  }
  s_active_cal_point = 0;
  s_is_collecting = false;
  highlight_active_marker();
}

static bool compute_calibration_from_samples(touch_transform_record_t *out) {
  if (!out)
    return false;

  lv_point_t raw_pts[CAL_POINT_COUNT];
  lv_point_t tgt_pts[CAL_POINT_COUNT];
  size_t used = 0;
  for (int i = 0; i < CAL_POINT_COUNT; ++i) {
    if (!s_cal_points[i].captured)
      continue;
    raw_pts[used] = s_cal_points[i].measured_raw;
    tgt_pts[used] = s_cal_points[i].target;
    used++;
  }

  if (used < 3)
    return false;

  touch_transform_record_t rec = {0};
  rec.magic = TOUCH_FOURCC_TO_U32('T', 'C', 'A', 'L');
  rec.version = 1;
  rec.generation = s_current_record.generation;
  rec.transform.swap_xy = s_current_record.transform.swap_xy;
  rec.transform.mirror_x = s_current_record.transform.mirror_x;
  rec.transform.mirror_y = s_current_record.transform.mirror_y;

  touch_transform_metrics_t metrics = {0};
  esp_err_t err = touch_transform_solve_affine(raw_pts, tgt_pts, used,
                                               &rec.transform, &metrics);
  if (err != ESP_OK || metrics.rms_error > 12.0f ||
      metrics.condition_number > 5000.0f) {
    ESP_LOGW(TAG, "Affine solve fallback rms=%.2f cond=%.1f err=%s",
             (double)metrics.rms_error, (double)metrics.condition_number,
             esp_err_to_name(err));
    err = touch_transform_solve_fallback(raw_pts, tgt_pts, used,
                                         &rec.transform);
    if (err != ESP_OK) {
      return false;
    }
    // recompute simple metrics for fallback
    float sum_sq = 0.0f;
    float max_err = 0.0f;
    for (size_t i = 0; i < used; ++i) {
      lv_point_t mapped;
      touch_transform_apply(&rec.transform, raw_pts[i].x, raw_pts[i].y, -1, -1,
                            &mapped);
      float dx = (float)mapped.x - (float)tgt_pts[i].x;
      float dy = (float)mapped.y - (float)tgt_pts[i].y;
      float err_pt = sqrtf(dx * dx + dy * dy);
      sum_sq += err_pt * err_pt;
      if (err_pt > max_err)
        max_err = err_pt;
    }
    metrics.rms_error = sqrtf(sum_sq / (float)used);
    metrics.max_error = max_err;
  }
  s_last_metrics = metrics;
  *out = rec;
  return true;
}

static void finalize_calibration(void) {
  touch_transform_record_t rec = {0};
  if (!compute_calibration_from_samples(&rec)) {
    ui_show_error("Points insuffisants ou mauvaise capture.");
    return;
  }

  s_current_record = rec;
  stop_capture_layer();

  apply_config_to_driver(false);
  update_orientation_label();
  set_progress_text("Calibration calculee RMS=%.2fpx max=%.2fpx",
                    (double)s_last_metrics.rms_error,
                    (double)s_last_metrics.max_error);
  ui_show_toast("Calibration tactile mise a jour", UI_TOAST_SUCCESS);
}

static void calibration_touch_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_PRESSED)
    return;
  if (!s_is_collecting || s_active_cal_point >= CAL_POINT_COUNT)
    return;

  lv_indev_t *indev = lv_indev_get_act();
  if (!indev)
    return;

  touch_sample_raw_t sample =
      touch_transform_sample_raw_oriented(app_board_get_touch_handle(), true);
  lv_point_t pt = {.x = (lv_coord_t)sample.raw_x, .y = (lv_coord_t)sample.raw_y};

  s_cal_points[s_active_cal_point].measured_raw = pt;
  s_cal_points[s_active_cal_point].captured = true;

  s_active_cal_point++;
  if (s_active_cal_point >= CAL_POINT_COUNT) {
    set_progress_text("Calcul en cours...");
    finalize_calibration();
  } else {
    highlight_active_marker();
    set_progress_text("Taper sur la croix %d/%d", s_active_cal_point + 1,
                      CAL_POINT_COUNT);
  }
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
    s_current_record.transform.swap_xy = enabled;
    break;
  case CAL_FLAG_MIRROR_X:
    s_current_record.transform.mirror_x = enabled;
    break;
  case CAL_FLAG_MIRROR_Y:
    s_current_record.transform.mirror_y = enabled;
    break;
  }

  apply_config_to_driver(false);
  sync_switch_states();
}

static void auto_detect_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  touch_sample_raw_t sample =
      touch_transform_sample_raw_oriented(app_board_get_touch_handle(), true);
  s_auto_probe.origin.x = sample.raw_x;
  s_auto_probe.origin.y = sample.raw_y;
  s_auto_probe.delta.x = 0;
  s_auto_probe.delta.y = 0;
  s_auto_probe.start_tick = lv_tick_get();
  stop_auto_timer();
  s_auto_timer = lv_timer_create(auto_timer_cb, 60, NULL);
  set_progress_text("Glissez vers la droite puis vers le bas pour auto-orienter");
  ui_show_toast("Bougez lentement votre doigt pour deduire l'orientation",
                UI_TOAST_INFO);
}

static void reset_defaults_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  touch_transform_identity(&s_current_record.transform);
  stop_capture_layer();
  apply_config_to_driver(false);
  sync_switch_states();
  set_progress_text("Calibration par defaut appliquee");
  ui_show_toast("Calibration par defaut appliquee", UI_TOAST_INFO);
}

static void start_capture_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  if (!s_capture_layer) {
    ui_show_error("Calque de capture absent");
    return;
  }

  // Force identity scale during acquisition to avoid compenser deux fois
  apply_config_to_driver(true);
  reset_cal_points(s_capture_layer);
  lv_obj_clear_flag(s_capture_layer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_capture_layer, LV_OBJ_FLAG_CLICKABLE);
  s_is_collecting = true;
  s_active_cal_point = 0;
  highlight_active_marker();
  set_progress_text("Taper sur la croix 1/%d", CAL_POINT_COUNT);
  ui_show_toast("Touchez chaque croix pour calibrer", UI_TOAST_INFO);
}

static void save_and_finish_cb(lv_event_t *e) {
  if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  s_current_record.magic = TOUCH_FOURCC_TO_U32('T', 'C', 'A', 'L');
  s_current_record.version = 1;

  esp_err_t err = touch_transform_storage_save(&s_current_record);
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

  // Main body - compact layout
  lv_obj_t *body = lv_obj_create(scr);
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t body_h = lv_display_get_vertical_resolution(disp) - UI_HEADER_HEIGHT - UI_SPACE_SM;
  lv_obj_set_size(body, LV_PCT(100), body_h);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_HEADER_HEIGHT);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, UI_SPACE_SM, 0);
  lv_obj_set_style_pad_gap(body, UI_SPACE_SM, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Short description
  lv_obj_t *desc = lv_label_create(body);
  lv_label_set_text(desc,
                    "Verifiez que le doigt suit bien les croix rouges. Activez Swap/Miroir si le pointeur est inverse, puis validez pour poursuivre.");
  lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(desc, LV_PCT(100));
  lv_obj_add_style(desc, &ui_style_text_body, 0);
  lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

  // Toggles in horizontal row
  lv_obj_t *toggles = lv_obj_create(body);
  lv_obj_set_style_bg_opa(toggles, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(toggles, 0, 0);
  lv_obj_set_style_pad_all(toggles, 0, 0);
  lv_obj_set_style_pad_gap(toggles, UI_SPACE_SM, 0);
  lv_obj_set_flex_flow(toggles, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(toggles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(toggles, LV_PCT(100));

  // Inline toggle helper - compact version
  {
    lv_obj_t *lbl1 = lv_label_create(toggles);
    lv_label_set_text(lbl1, "Swap:");
    lv_obj_add_style(lbl1, &ui_style_text_body, 0);
    s_switch_swap = lv_switch_create(toggles);
    if (s_current_record.transform.swap_xy) lv_obj_add_state(s_switch_swap, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch_swap, on_toggle_event, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)CAL_FLAG_SWAP);

    lv_obj_t *lbl2 = lv_label_create(toggles);
    lv_label_set_text(lbl2, "MirX:");
    lv_obj_add_style(lbl2, &ui_style_text_body, 0);
    s_switch_mir_x = lv_switch_create(toggles);
    if (s_current_record.transform.mirror_x) lv_obj_add_state(s_switch_mir_x, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch_mir_x, on_toggle_event, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)CAL_FLAG_MIRROR_X);

    lv_obj_t *lbl3 = lv_label_create(toggles);
    lv_label_set_text(lbl3, "MirY:");
    lv_obj_add_style(lbl3, &ui_style_text_body, 0);
    s_switch_mir_y = lv_switch_create(toggles);
    if (s_current_record.transform.mirror_y) lv_obj_add_state(s_switch_mir_y, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch_mir_y, on_toggle_event, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)CAL_FLAG_MIRROR_Y);
  }

  // Info card - compact
  lv_obj_t *card = lv_obj_create(body);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_style_pad_all(card, UI_SPACE_SM, 0);
  lv_obj_set_style_radius(card, UI_RADIUS_SM, 0);
  lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, UI_SPACE_XS, 0);

  s_orientation_label = lv_label_create(card);
  lv_obj_add_style(s_orientation_label, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(s_orientation_label, lv_color_white(), 0);
  update_orientation_label();

  s_touch_dbg_label = lv_label_create(card);
  lv_obj_set_style_bg_opa(s_touch_dbg_label, LV_OPA_40, 0);
  lv_obj_set_style_bg_color(s_touch_dbg_label, lv_color_black(), 0);
  lv_obj_set_style_text_color(s_touch_dbg_label, lv_color_white(), 0);
  lv_obj_set_style_pad_all(s_touch_dbg_label, UI_SPACE_XS, 0);
  lv_label_set_text(s_touch_dbg_label, "touch: --");

  // Progress label
  s_cal_progress_label = lv_label_create(body);
  lv_obj_add_style(s_cal_progress_label, &ui_style_text_body, 0);
  lv_obj_set_style_text_align(s_cal_progress_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_cal_progress_label, LV_PCT(100));
  set_progress_text("Appuyez sur \"Calibrer\" puis touchez les 5 croix pour ajuster le GT911.");

  // Action buttons - compact
  lv_obj_t *actions = lv_obj_create(body);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_pad_all(actions, 0, 0);
  lv_obj_set_style_pad_gap(actions, UI_SPACE_SM, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(actions, LV_PCT(100));

  lv_obj_t *btn_calibrate = lv_button_create(actions);
  lv_obj_add_style(btn_calibrate, &ui_style_btn_primary, 0);
  lv_obj_set_style_min_width(btn_calibrate, 140, 0);
  lv_obj_set_style_min_height(btn_calibrate, 44, 0);
  lv_obj_add_event_cb(btn_calibrate, start_capture_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_calibrate), "Calibrer (5 points)");

  lv_obj_t *btn_reset = lv_button_create(actions);
  lv_obj_add_style(btn_reset, &ui_style_btn_secondary, 0);
  lv_obj_set_style_min_width(btn_reset, 120, 0);
  lv_obj_set_style_min_height(btn_reset, 44, 0);
  lv_obj_add_event_cb(btn_reset, reset_defaults_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_reset), "Par defaut");

  lv_obj_t *btn_validate = lv_button_create(actions);
  lv_obj_add_style(btn_validate, &ui_style_btn_primary, 0);
  lv_obj_set_style_min_width(btn_validate, 120, 0);
  lv_obj_set_style_min_height(btn_validate, 44, 0);
  lv_obj_add_event_cb(btn_validate, save_and_finish_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(btn_validate), "Enregistrer");

  // Visual markers to verify orientation (non-interactive)
  lv_obj_t *overlay = lv_obj_create(scr);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_add_event_cb(overlay, calibration_touch_cb, LV_EVENT_PRESSED, NULL);
  s_capture_layer = overlay;
  lv_obj_add_flag(s_capture_layer, LV_OBJ_FLAG_HIDDEN);
  reset_cal_points(overlay);

  if (s_touch_dbg_timer) {
    lv_timer_del(s_touch_dbg_timer);
  }
  s_touch_dbg_timer = lv_timer_create(touch_debug_timer_cb, 200, s_touch_dbg_label);

  ui_switch_screen(scr, LV_SCR_LOAD_ANIM_NONE);
}

void ui_calibration_apply(const touch_transform_record_t *rec) {
  if (rec) {
    s_current_record = *rec;
  } else {
    touch_transform_identity(&s_current_record.transform);
  }
  apply_config_to_driver(false);
  sync_switch_states();
}

bool ui_calibration_check_and_start(void) {
  touch_transform_record_t rec = {0};
  esp_err_t err = touch_transform_storage_load(&rec);
  if (err == ESP_OK) {
    ui_calibration_apply(&rec);
    return false;
  }

  ESP_LOGW(TAG, "Calibration missing or invalid (%s); starting wizard",
           esp_err_to_name(err));
  ui_calibration_start();
  return true;
}

void ui_calibration_start(void) {
  ESP_LOGI(TAG, "Starting Calibration UI");

  if (touch_transform_storage_load(&s_current_record) != ESP_OK) {
    touch_transform_identity(&s_current_record.transform);
    s_current_record.magic = TOUCH_TRANSFORM_MAGIC;
    s_current_record.version = TOUCH_TRANSFORM_VERSION;
  }
  apply_config_to_driver(false);
  build_screen();
}
