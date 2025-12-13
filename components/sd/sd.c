#include "sd.h"
#include "esp_log.h"
#include "sd_host_extcs.h"
#include "sdkconfig.h"
#include <string.h>


static const char *TAG = "sd";

// Global handle
sdmmc_card_t *card = NULL;

esp_err_t sd_card_init() {
  ESP_LOGI(TAG, "Initializing SD (ExtCS Mode)...");

  esp_err_t ret = sd_extcs_mount_card(MOUNT_POINT, 5);
  if (ret == ESP_OK) {
    card = sd_extcs_get_card_handle();
  }
  return ret;
}

void sd_card_print_info() {
  if (card)
    sdmmc_card_print_info(stdout, card);
}

esp_err_t sd_mmc_unmount() {
  esp_err_t ret = sd_extcs_unmount_card(MOUNT_POINT);
  if (ret == ESP_OK) {
    card = NULL;
  }
  return ret;
}

esp_err_t read_sd_capacity(size_t *total_capacity, size_t *available_capacity) {
  FATFS *fs;
  DWORD free_clusters;
  FRESULT res = f_getfree(MOUNT_POINT, &free_clusters, &fs);
  if (res != FR_OK) {
    ESP_LOGE(TAG, "f_getfree failed: %d", res);
    return ESP_FAIL;
  }

  uint64_t total_sectors = ((uint64_t)(fs->n_fatent - 2)) * fs->csize;
  uint64_t free_sectors = ((uint64_t)free_clusters) * fs->csize;

  if (total_capacity)
    *total_capacity = (total_sectors * fs->ssize) / 1024;
  if (available_capacity)
    *available_capacity = (free_sectors * fs->ssize) / 1024;

  return ESP_OK;
}
