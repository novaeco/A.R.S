#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "board.h"
#include "i2c_bus_shared.h"
#include "io_extension.h"
#include "display_driver.h"
#include "lvgl_port.h"
#include "touch_gt911.h"
#include "touch_transform.h"
#include "sd_service.h"
#include "storage_core.h"
#include "domain_models.h"
#include "compliance_rules.h"
#include "documents_service.h"
#include "export_share.h"
#include "ui_app.h"

static const char *TAG = "app_main";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boot start");
    init_nvs();

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(i2c_bus_shared_init());
    ESP_ERROR_CHECK(io_extension_init());

    ESP_ERROR_CHECK(display_driver_init());
    ESP_ERROR_CHECK(lvgl_port_init());
    ESP_ERROR_CHECK(touch_gt911_init());

    sd_card_state_t sd_state = sd_service_init();
    if (sd_state != SD_CARD_MOUNTED) {
        ESP_LOGW(TAG, "SD card not mounted, continuing without external storage");
    }

    storage_context_t storage_ctx = {0};
    ESP_ERROR_CHECK(storage_core_init(&storage_ctx));
    domain_models_register_builtin(&storage_ctx);
    compliance_rules_register_builtin(&storage_ctx);

    documents_service_init(&storage_ctx);
    export_share_init(&storage_ctx);

    ui_app_start(&storage_ctx);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
