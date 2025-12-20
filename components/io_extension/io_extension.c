#include "io_extension.h"
#include "i2c_bus_shared.h"
#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "io_ext";

static uint8_t shadow_outputs = 0;

esp_err_t io_extension_init(void)
{
    ESP_LOGI(TAG, "Initializing CH32V003 IO extension at 0x%02X", BOARD_IO_EXT_ADDR);
    // Without hardware map, keep a shadow register for sanity.
    shadow_outputs = 0;
    return ESP_OK;
}

esp_err_t io_extension_set_output(uint8_t pin, bool level)
{
    shadow_outputs = (level) ? (shadow_outputs | (1 << pin)) : (shadow_outputs & ~(1 << pin));
    uint8_t payload[2] = {pin, (uint8_t)level};
    esp_err_t ret = i2c_bus_shared_write(BOARD_IO_EXT_ADDR, payload, sizeof(payload));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "io_extension write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t io_extension_get_input(uint8_t pin, bool *level)
{
    if (!level) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value = 0;
    esp_err_t ret = i2c_bus_shared_read(BOARD_IO_EXT_ADDR, &value, 1);
    if (ret == ESP_OK) {
        *level = value & (1 << pin);
    } else {
        ESP_LOGW(TAG, "io_extension read failed: %s", esp_err_to_name(ret));
        *level = false;
    }
    return ret;
}

esp_err_t io_extension_pulse(uint8_t pin, uint32_t duration_ms)
{
    ESP_RETURN_ON_ERROR(io_extension_set_output(pin, true), TAG, "set high");
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return io_extension_set_output(pin, false);
}
