#include "touch_gt911.h"
#include "i2c_bus_shared.h"
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define GT911_I2C_ADDR 0x5D
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINTS 0x814F
#define GT911_CONFIG_START 0x8047
#define GT911_CONFIG_SIZE 186
#define GT911_REG_CONFIG_CHECKSUM 0x80FF
#define GT911_REG_CONFIG_FRESH 0x8100
#define GT911_POINT_SIZE 8
#define GT911_MAX_POINTS 5

static const char *TAG = "touch_gt911";
static volatile bool s_irq_flag = false;

static void IRAM_ATTR touch_irq_handler(void *arg)
{
    (void)arg;
    s_irq_flag = true;
}

static esp_err_t gt911_write(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[GT911_CONFIG_SIZE + 2];
    if (len + 2 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    memcpy(&buf[2], data, len);

    SemaphoreHandle_t mutex = i2c_bus_shared_get_mutex();
    if (!mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_write_to_device(i2c_bus_shared_port(), GT911_I2C_ADDR, buf, len + 2, pdMS_TO_TICKS(100));
    xSemaphoreGive(mutex);
    return ret;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
    SemaphoreHandle_t mutex = i2c_bus_shared_get_mutex();
    if (!mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = i2c_master_write_read_device(i2c_bus_shared_port(), GT911_I2C_ADDR, reg_buf, sizeof(reg_buf), data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(mutex);
    return ret;
}

static esp_err_t gt911_update_resolution(void)
{
    uint8_t cfg[GT911_CONFIG_SIZE] = {0};
    ESP_RETURN_ON_ERROR(gt911_read(GT911_CONFIG_START, cfg, sizeof(cfg)), TAG, "read cfg");

    cfg[1] = (uint8_t)(BOARD_DISPLAY_H_RES & 0xFF);
    cfg[2] = (uint8_t)(BOARD_DISPLAY_H_RES >> 8);
    cfg[3] = (uint8_t)(BOARD_DISPLAY_V_RES & 0xFF);
    cfg[4] = (uint8_t)(BOARD_DISPLAY_V_RES >> 8);
    cfg[5] = GT911_MAX_POINTS;

    uint8_t checksum = 0;
    for (size_t i = 0; i < GT911_CONFIG_SIZE - 2; ++i) {
        checksum += cfg[i];
    }
    checksum = (~checksum) + 1;
    cfg[GT911_CONFIG_SIZE - 2] = checksum;
    cfg[GT911_CONFIG_SIZE - 1] = 0x01;

    ESP_RETURN_ON_ERROR(gt911_write(GT911_CONFIG_START, cfg, sizeof(cfg)), TAG, "write cfg");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t touch_gt911_init(void)
{
    ESP_LOGI(TAG, "GT911 init on shared I2C, IRQ GPIO%d, reset EXIO%d", BOARD_TOUCH_IRQ_GPIO, BOARD_TOUCH_RST_EXIO);

    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_IRQ_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq_cfg), TAG, "irq gpio");
    if (gpio_install_isr_service(0) != ESP_OK) {
        // ISR service may already be installed elsewhere
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BOARD_TOUCH_IRQ_GPIO, touch_irq_handler, NULL), TAG, "irq handler");
    s_irq_flag = false;

    ESP_RETURN_ON_ERROR(board_touch_reset_pulse(), TAG, "reset");

    uint8_t product[4] = {0};
    ESP_RETURN_ON_ERROR(gt911_read(GT911_REG_PRODUCT_ID, product, sizeof(product)), TAG, "product");
    ESP_LOGI(TAG, "GT911 product id: %c%c%c%c", product[0], product[1], product[2], product[3]);

    return gt911_update_resolution();
}

bool touch_gt911_read(touch_points_t *points)
{
    if (!points) {
        return false;
    }
    memset(points, 0, sizeof(*points));

    bool irq_level = gpio_get_level(BOARD_TOUCH_IRQ_GPIO);
    if (!s_irq_flag && irq_level) {
        return false;
    }
    s_irq_flag = false;

    uint8_t status = 0;
    if (gt911_read(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        return false;
    }

    uint8_t touch_num = status & 0x0F;
    if (!(status & 0x80) || touch_num == 0 || touch_num > GT911_MAX_POINTS) {
        uint8_t clear = 0;
        gt911_write(GT911_REG_STATUS, &clear, 1);
        return false;
    }

    uint8_t buf[GT911_POINT_SIZE * GT911_MAX_POINTS] = {0};
    size_t read_len = touch_num * GT911_POINT_SIZE;
    if (gt911_read(GT911_REG_POINTS, buf, read_len) != ESP_OK) {
        return false;
    }

    for (uint8_t i = 0; i < touch_num; ++i) {
        size_t offset = i * GT911_POINT_SIZE;
        uint8_t track_id = buf[offset];
        uint16_t x = ((uint16_t)buf[offset + 2] << 8) | buf[offset + 1];
        uint16_t y = ((uint16_t)buf[offset + 4] << 8) | buf[offset + 3];
        if (x >= BOARD_DISPLAY_H_RES || y >= BOARD_DISPLAY_V_RES) {
            continue;
        }
        points->points[points->count].track_id = track_id;
        points->points[points->count].x = x;
        points->points[points->count].y = y;
        points->count++;
    }

    uint8_t clear = 0;
    gt911_write(GT911_REG_STATUS, &clear, 1);
    return points->count > 0;
}
