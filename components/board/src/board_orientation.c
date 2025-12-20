#include "board_orientation.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board_orient";

#ifndef CONFIG_ARS_TOUCH_SWAP_XY
#define CONFIG_ARS_TOUCH_SWAP_XY 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_X
#define CONFIG_ARS_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_Y
#define CONFIG_ARS_TOUCH_MIRROR_Y 0
#endif

#if CONFIG_ARS_DISPLAY_ROTATION_90
#define BOARD_ROTATION_DEG 90
#elif CONFIG_ARS_DISPLAY_ROTATION_180
#define BOARD_ROTATION_DEG 180
#elif CONFIG_ARS_DISPLAY_ROTATION_270
#define BOARD_ROTATION_DEG 270
#else
#define BOARD_ROTATION_DEG 0
#endif

static lv_display_rotation_t map_rotation(void) {
  switch (BOARD_ROTATION_DEG) {
  case 90:
    return LV_DISPLAY_ROTATION_90;
  case 180:
    return LV_DISPLAY_ROTATION_180;
  case 270:
    return LV_DISPLAY_ROTATION_270;
  default:
    return LV_DISPLAY_ROTATION_0;
  }
}

void board_orientation_get_defaults(board_orientation_t *out) {
  if (!out)
    return;
  *out = (board_orientation_t){
      .swap_xy = CONFIG_ARS_TOUCH_SWAP_XY,
      .mirror_x = CONFIG_ARS_TOUCH_MIRROR_X,
      .mirror_y = CONFIG_ARS_TOUCH_MIRROR_Y,
      .rotation = map_rotation(),
  };
}

void board_orientation_apply_display(lv_display_t *disp,
                                     const board_orientation_t *cfg) {
  if (!disp || !cfg)
    return;
  lv_display_set_rotation(disp, cfg->rotation);
  ESP_LOGI(TAG, "Display rotation set to %d deg", BOARD_ROTATION_DEG);
}

void board_orientation_apply_touch_defaults(touch_orient_config_t *cfg,
                                            const board_orientation_t *orient) {
  if (!cfg || !orient)
    return;
  cfg->swap_xy = orient->swap_xy;
  cfg->mirror_x = orient->mirror_x;
  cfg->mirror_y = orient->mirror_y;
}

