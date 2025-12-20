#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t track_id;
} touch_point_t;

typedef struct {
    uint8_t count;
    touch_point_t points[5];
} touch_points_t;

esp_err_t touch_gt911_init(void);
bool touch_gt911_read(touch_points_t *points);
