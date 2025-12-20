#include "i2c_bus_shared.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static SemaphoreHandle_t s_i2c_mutex;
static bool s_init_done = false;

esp_err_t i2c_bus_shared_init(void)
{
    if (s_init_done) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &conf), TAG, "config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(BOARD_I2C_PORT, conf.mode, 0, 0, 0), TAG, "install");

    s_i2c_mutex = xSemaphoreCreateMutex();
    if (!s_i2c_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_init_done = true;
    ESP_LOGI(TAG, "I2C shared bus ready on port %d", BOARD_I2C_PORT);
    return ESP_OK;
}

SemaphoreHandle_t i2c_bus_shared_get_mutex(void)
{
    return s_i2c_mutex;
}

i2c_port_t i2c_bus_shared_port(void)
{
    return BOARD_I2C_PORT;
}

esp_err_t i2c_bus_shared_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!s_i2c_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_read_from_device(BOARD_I2C_PORT, addr, data, len, pdMS_TO_TICKS(100));
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
    esp_err_t ret = i2c_master_write_to_device(BOARD_I2C_PORT, addr, data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}
