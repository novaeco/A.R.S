#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus_shared.h"
#include "io_extension.h"

static const char *TAG = "board";

static esp_err_t configure_touch_irq_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_IRQ_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

esp_err_t board_lcd_power_set(bool on)
{
    return io_ext_set(BOARD_LCD_POWER_EXIO, on);
}

esp_err_t board_backlight_set(bool on)
{
    return io_ext_set(BOARD_LCD_BACKLIGHT_EXIO, on);
}

esp_err_t board_touch_reset_pulse(void)
{
    ESP_RETURN_ON_ERROR(io_ext_set(BOARD_TOUCH_RST_EXIO, false), TAG, "rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(io_ext_set(BOARD_TOUCH_RST_EXIO, true), TAG, "rst high");
    vTaskDelay(pdMS_TO_TICKS(60));
    return ESP_OK;
}

esp_err_t board_sd_cs_set(bool asserted)
{
    return io_ext_set(BOARD_SD_CS_EXIO, asserted ? 0 : 1);
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_shared_init(BOARD_I2C_SDA, BOARD_I2C_SCL), TAG, "i2c shared");
    ESP_RETURN_ON_ERROR(io_ext_init(BOARD_IO_EXT_ADDR, i2c_bus_shared_port(), i2c_bus_shared_get_mutex()), TAG, "io ext");
    ESP_RETURN_ON_ERROR(configure_touch_irq_gpio(), TAG, "touch irq gpio");

    ESP_RETURN_ON_ERROR(board_lcd_power_set(true), TAG, "lcd power");
    ESP_RETURN_ON_ERROR(board_backlight_set(true), TAG, "backlight");

    ESP_LOGI(TAG, "Board initialized: I2C on GPIO%d/%d, IO expander 0x%02X", BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_IO_EXT_ADDR);
    return ESP_OK;
}
