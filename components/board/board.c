#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "board";

esp_err_t board_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_LCD_DISP_EN) | (1ULL << BOARD_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(BOARD_LCD_DISP_EN, 1);
    gpio_set_level(BOARD_LCD_BACKLIGHT, 1);

    ESP_LOGI(TAG, "Board initialized (display enable/backlight asserted)");
    return ESP_OK;
}
