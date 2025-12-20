#include "i2c_bus_shared.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static SemaphoreHandle_t s_i2c_mutex;
static bool s_init_done = false;
static gpio_num_t s_sda = GPIO_NUM_NC;
static gpio_num_t s_scl = GPIO_NUM_NC;

esp_err_t i2c_bus_shared_init(gpio_num_t sda_io, gpio_num_t scl_io)
{
    if (s_init_done) {
        return ESP_OK;
    }

    s_sda = sda_io;
    s_scl = scl_io;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_sda,
        .scl_io_num = s_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &conf), TAG, "config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0), TAG, "install");

    s_i2c_mutex = xSemaphoreCreateMutex();
    if (!s_i2c_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_init_done = true;
    ESP_LOGI(TAG, "I2C shared bus ready on SDA %d / SCL %d", s_sda, s_scl);
    return ESP_OK;
}

SemaphoreHandle_t i2c_bus_shared_get_mutex(void)
{
    return s_i2c_mutex;
}

i2c_port_t i2c_bus_shared_port(void)
{
    return I2C_NUM_0;
}

esp_err_t i2c_bus_shared_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!s_i2c_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_read_from_device(I2C_NUM_0, addr, data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}

esp_err_t i2c_bus_shared_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!s_i2c_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, addr, data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}
