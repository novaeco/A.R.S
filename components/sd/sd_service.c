#include "sd_service.h"
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "io_extension.h"
#include "board.h"

static const char *TAG = "sd_service";
static sd_card_state_t s_state = SD_CARD_NOT_PRESENT;
static sdmmc_card_t *s_card = NULL;
static const char *s_mount_point = "/sdcard";

sd_card_state_t sd_service_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = {
        .host_id = host.slot,
        .gpio_cs = -1,
        .gpio_cd = -1,
        .gpio_wp = -1,
        .gpio_int = -1,
    };

    // CS controlled through IO extension; best-effort toggle.
    io_extension_set_output(BOARD_SD_CS_IO_EXT_PIN, true);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(s_mount_point, &host, &slot_config, &mount_config, &s_card);
    if (ret == ESP_OK) {
        s_state = SD_CARD_MOUNTED;
        ESP_LOGI(TAG, "SD mounted at %s", s_mount_point);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        s_state = SD_CARD_NOT_PRESENT;
        ESP_LOGW(TAG, "SD card not detected: continuing without it");
    } else {
        s_state = SD_CARD_ERROR;
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
    }
    return s_state;
}

esp_err_t sd_service_get_mount_point(const char **path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    *path = (s_state == SD_CARD_MOUNTED) ? s_mount_point : NULL;
    return ESP_OK;
}
