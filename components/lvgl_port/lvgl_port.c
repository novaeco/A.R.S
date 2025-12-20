#include "lvgl_port.h"
#include "display_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "esp_heap_caps.h"

static const char *TAG = "lvgl_port";
static lv_display_t *s_disp = NULL;
static TaskHandle_t s_lvgl_task;

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
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t lvgl_port_init(void)
{
    if (s_disp) {
        return ESP_OK;
    }
    lv_init();

    size_t buf_size = BOARD_DISPLAY_H_RES * 40 * sizeof(lv_color_t);
    lv_color_t *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(BOARD_DISPLAY_H_RES, BOARD_DISPLAY_V_RES);
    lv_display_set_flush_cb(s_disp, display_driver_flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

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
