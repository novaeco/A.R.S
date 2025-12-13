#include "ui_screen_manager.h"
#include "esp_log.h"
#include "lvgl.h"

static lv_obj_t *loading_overlay = NULL;
static lv_obj_t *error_toast = NULL;

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

static void error_toast_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

static void error_toast_delete_cb(lv_anim_t *a) {
  lv_obj_del((lv_obj_t *)a->var);
  error_toast = NULL;
}

void ui_show_error(const char *msg) {
  if (error_toast) {
    lv_obj_del(error_toast);
  }

  error_toast = lv_obj_create(lv_layer_top());
  lv_obj_set_style_bg_color(error_toast, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_size(error_toast, LV_PCT(80), 60);
  lv_obj_align(error_toast, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_radius(error_toast, 10, 0);

  lv_obj_t *lbl = lv_label_create(error_toast);
  lv_label_set_text(lbl, msg);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_center(lbl);

  // Fade out animation
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, error_toast);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 500);
  lv_anim_set_delay(&a, 2000);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&a, error_toast_anim_cb);
  lv_anim_set_completed_cb(&a, error_toast_delete_cb);
  lv_anim_start(&a);
}
