#include "display_driver.h"
#include "esp_log.h"
#include "board.h"

static const char *TAG = "display";

esp_err_t display_driver_init(void)
{
    ESP_LOGI(TAG, "Display driver init stub for %dx%d", BOARD_DISPLAY_H_RES, BOARD_DISPLAY_V_RES);
    return ESP_OK;
}

void display_driver_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *color_map)
{
    (void)area;
    (void)color_map;
    lv_display_flush_ready(display);
}
