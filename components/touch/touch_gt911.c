#include "touch_gt911.h"
#include "i2c_bus_shared.h"
#include "io_extension.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "touch_gt911";

esp_err_t touch_gt911_init(void)
{
    ESP_LOGI(TAG, "GT911 init: reset via IO extension pins %d/%d", BOARD_TOUCH_RST_IO_EXT_PIN, BOARD_TOUCH_IRQ_IO_EXT_PIN);
    // Use IO extender to reset controller if needed
    io_extension_pulse(BOARD_TOUCH_RST_IO_EXT_PIN, 10);
    return ESP_OK;
}

bool touch_gt911_read(touch_point_t *point)
{
    if (!point) {
        return false;
    }
    uint8_t buf[5] = {0};
    if (i2c_bus_shared_read(0x5D, buf, sizeof(buf)) != ESP_OK) {
        point->touched = false;
        return false;
    }
    point->touched = buf[0] != 0;
    if (point->touched) {
        point->x = (buf[2] << 8) | buf[1];
        point->y = (buf[4] << 8) | buf[3];
        ESP_LOGD(TAG, "Touch at %u,%u", point->x, point->y);
    }
    return point->touched;
}
