#include "ui_screen_manager.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui_theme.h"

static lv_obj_t *loading_overlay = NULL;
static lv_obj_t *toast_box = NULL;
static const char *TAG = "ui_screen_mgr";

void ui_screen_manager_init(void) {
  // nothing specific unless we pre-create overlays
}

void ui_switch_screen(lv_obj_t *screen, lv_scr_load_anim_t anim) {
  if (!screen)
    return;
  // Auto-delete the old screen when animation finishes to save RAM
  lv_scr_load_anim(screen, anim, 400, 0, true);
}

void ui_show_loading(bool show) {
  if (show) {
    if (!loading_overlay) {
      loading_overlay = lv_obj_create(lv_layer_top());
      lv_obj_set_size(loading_overlay, LV_PCT(100), LV_PCT(100));
      lv_obj_set_style_bg_color(loading_overlay, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(loading_overlay, LV_OPA_50, 0);
      lv_obj_clear_flag(loading_overlay, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t *spin = lv_spinner_create(loading_overlay);
      lv_obj_set_size(spin, 60, 60);
      lv_obj_center(spin);
    }
    lv_obj_clear_flag(loading_overlay, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (loading_overlay) {
      lv_obj_add_flag(loading_overlay, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void toast_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

static void toast_delete_cb(lv_anim_t *a) {
  lv_obj_del((lv_obj_t *)a->var);
  toast_box = NULL;
}

void ui_screen_claim_with_theme(lv_obj_t *screen, const char *name) {
  if (!screen)
    return;

  const char *label = name ? name : "<screen>";
  if (!lvgl_port_in_task_context()) {
    ESP_LOGE(TAG,
             "Screen %s created outside LVGL task context; use LVGL dispatcher",
             label);
  }

  ui_theme_apply(screen);
}

static lv_color_t ui_toast_color(ui_toast_type_t type) {
  switch (type) {
  case UI_TOAST_SUCCESS:
    return UI_COLOR_SUCCESS;
  case UI_TOAST_ERROR:
    return UI_COLOR_DANGER;
  case UI_TOAST_INFO:
  default:
    return UI_COLOR_PRIMARY;
  }
}

void ui_show_toast(const char *msg, ui_toast_type_t type) {
  if (!msg)
    return;

  if (toast_box) {
    lv_obj_del(toast_box);
    toast_box = NULL;
  }

  toast_box = lv_obj_create(lv_layer_top());
  lv_obj_set_style_bg_color(toast_box, ui_toast_color(type), 0);
  lv_obj_set_size(toast_box, LV_PCT(80), 64);
  lv_obj_align(toast_box, LV_ALIGN_TOP_MID, 0, UI_SPACE_MD);
  lv_obj_set_style_radius(toast_box, UI_RADIUS_MD, 0);
  lv_obj_clear_flag(toast_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_shadow_width(toast_box, UI_SHADOW_MD, 0);
  lv_obj_set_style_shadow_color(toast_box, ui_toast_color(type), 0);
  lv_obj_set_style_pad_all(toast_box, UI_SPACE_MD, 0);

  lv_obj_t *lbl = lv_label_create(toast_box);
  lv_label_set_text(lbl, msg);
  lv_obj_add_style(lbl, &ui_style_text_body, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_center(lbl);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, toast_box);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 400);
  lv_anim_set_delay(&a, 2200);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&a, toast_anim_cb);
  lv_anim_set_completed_cb(&a, toast_delete_cb);
  lv_anim_start(&a);
}

void ui_show_error(const char *msg) { ui_show_toast(msg, UI_TOAST_ERROR); }
