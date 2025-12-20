#include "io_extension.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "io_ext";
static uint8_t s_addr = 0x00;
static i2c_port_t s_port = I2C_NUM_MAX;
static SemaphoreHandle_t s_mutex = NULL;
static uint8_t shadow_outputs = 0;

esp_err_t io_ext_init(uint8_t i2c_addr, i2c_port_t port, SemaphoreHandle_t mutex)
{
    if (!mutex) {
        return ESP_ERR_INVALID_ARG;
    }
    s_addr = i2c_addr;
    s_port = port;
    s_mutex = mutex;
    shadow_outputs = 0;
    ESP_LOGI(TAG, "Initializing IO extension at 0x%02X on port %d", s_addr, s_port);
    return ESP_OK;
}

static esp_err_t io_ext_write(uint8_t pin, uint8_t value)
{
    if (!s_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t payload[2] = {pin, value};
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_write_to_device(s_port, s_addr, payload, sizeof(payload), pdMS_TO_TICKS(100));
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t io_ext_set(uint8_t pin, bool level)
{
    shadow_outputs = (level) ? (shadow_outputs | (1 << pin)) : (shadow_outputs & ~(1 << pin));
    esp_err_t ret = io_ext_write(pin, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "io_ext_set failed on pin %u: %s", pin, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t io_ext_get(uint8_t pin, bool *level)
{
    if (!level || !s_mutex) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_read_from_device(s_port, s_addr, &value, 1, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_mutex);
    if (ret == ESP_OK) {
        *level = value & (1 << pin);
    } else {
        *level = false;
        ESP_LOGW(TAG, "io_ext_get failed on pin %u: %s", pin, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t io_ext_pulse(uint8_t pin, uint32_t duration_ms)
{
    ESP_RETURN_ON_ERROR(io_ext_set(pin, true), TAG, "set high");
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return io_ext_set(pin, false);
}
