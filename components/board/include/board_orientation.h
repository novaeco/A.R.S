#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "touch_orient.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool swap_xy;
  bool mirror_x;
  bool mirror_y;
  lv_display_rotation_t rotation;
} board_orientation_t;

void board_orientation_get_defaults(board_orientation_t *out);
void board_orientation_apply_display(lv_display_t *disp,
                                     const board_orientation_t *cfg);
void board_orientation_apply_touch_defaults(touch_orient_config_t *cfg,
                                            const board_orientation_t *orient);

#ifdef __cplusplus
}
#endif

