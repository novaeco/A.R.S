#pragma once
#include <stdint.h>
#include "board.h"

static inline void touch_transform_apply(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    if (out_x) {
        *out_x = raw_x % BOARD_DISPLAY_H_RES;
    }
    if (out_y) {
        *out_y = raw_y % BOARD_DISPLAY_V_RES;
    }
}
