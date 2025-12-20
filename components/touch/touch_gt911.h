#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool touched;
    uint16_t x;
    uint16_t y;
} touch_point_t;

esp_err_t touch_gt911_init(void);
bool touch_gt911_read(touch_point_t *point);
