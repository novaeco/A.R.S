#include "sd_service.h"
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "board.h"
#include "driver/spi_master.h"

static const char *TAG = "sd_service";
static sd_card_state_t s_state = SD_CARD_NOT_PRESENT;
static sdmmc_card_t *s_card = NULL;
static const char *s_mount_point = "/sdcard";

sd_card_state_t sd_service_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_SPI_MOSI,
        .miso_io_num = BOARD_SD_SPI_MISO,
        .sclk_io_num = BOARD_SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    if (spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bus for SD");
        s_state = SD_CARD_ERROR;
        return s_state;
    }

    sdspi_device_config_t slot_config = {
        .host_id = host.slot,
        .gpio_cs = BOARD_SD_DUMMY_CS,
        .gpio_cd = -1,
        .gpio_wp = -1,
        .gpio_int = -1,
    };

    // Keep real CS asserted through IO expander (sole device on bus)
    board_sd_cs_set(true);

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
        board_sd_cs_set(false);
    }
    if (ret != ESP_OK) {
        spi_bus_free(host.slot);
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
