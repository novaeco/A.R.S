#include "lvgl_port.h"
#include "display_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "touch_gt911.h"
#include "touch_transform.h"

static const char *TAG = "lvgl_port";
static lv_display_t *s_disp = NULL;
static TaskHandle_t s_lvgl_task;
static SemaphoreHandle_t s_lvgl_mutex;
static lv_indev_t *s_touch_indev = NULL;

static void lv_tick_task(void *arg)
{
    (void)arg;
    const TickType_t delay = pdMS_TO_TICKS(10);
    while (true) {
        lv_tick_inc(10);
        vTaskDelay(delay);
    }
}

static void lvgl_loop(void *arg)
{
    (void)arg;
    while (true) {
        if (s_lvgl_mutex && xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGiveRecursive(s_lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static bool lvgl_touch_read_cb(lv_indev_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    touch_points_t points = {0};
    if (!touch_gt911_read(&points) || points.count == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return false;
    }

    uint16_t tx = 0;
    uint16_t ty = 0;
    touch_transform_apply(points.points[0].x, points.points[0].y, &tx, &ty);
    data->point.x = tx;
    data->point.y = ty;
    data->state = LV_INDEV_STATE_PRESSED;
    return false;
}

esp_err_t lvgl_port_init(void)
{
    if (s_disp) {
        return ESP_OK;
    }
    lv_init();

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    size_t buf_size = BOARD_DISPLAY_H_RES * 40 * sizeof(lv_color_t);
#ifdef CONFIG_LVGL_LCD_BUF_SIZE
    buf_size = CONFIG_LVGL_LCD_BUF_SIZE;
#endif
    lv_color_t *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(BOARD_DISPLAY_H_RES, BOARD_DISPLAY_V_RES);
    lv_display_set_flush_cb(s_disp, display_driver_flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    display_driver_register_callbacks(s_disp);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    s_touch_indev = lv_indev_drv_register(&indev_drv);

    BaseType_t res = xTaskCreatePinnedToCore(lv_tick_task, "lv_tick", 2048, NULL, 5, NULL, 0);
    if (res != pdPASS) {
        return ESP_FAIL;
    }
    res = xTaskCreatePinnedToCore(lvgl_loop, "lv_loop", 4096, NULL, 5, &s_lvgl_task, 1);
    if (res != pdPASS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "LVGL initialized with display %dx%d", BOARD_DISPLAY_H_RES, BOARD_DISPLAY_V_RES);
    return ESP_OK;
}

lv_display_t *lvgl_port_get_display(void)
{
    return s_disp;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_mutex) {
        return false;
    }
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}
