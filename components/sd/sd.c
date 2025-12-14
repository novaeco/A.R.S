#include "sd.h"
#include "esp_log.h"
#include "sd_host_extcs.h"
#include "sdkconfig.h"
#include <dirent.h>
#include <string.h>


static const char *TAG = "sd";

// Global handle
sdmmc_card_t *card = NULL;
static sd_state_t s_state = SD_STATE_UNINITIALIZED;

static void sd_set_state(sd_state_t new_state) {
  if (s_state != new_state) {
    ESP_LOGI(TAG, "SD state -> %s", sd_state_str(new_state));
  }
  s_state = new_state;
}

esp_err_t sd_card_init() {
  ESP_LOGI(TAG, "Initializing SD (ExtCS Mode)...");

  esp_err_t ret = sd_extcs_register_io_extender(IO_EXTENSION_Get_Handle());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SD IOEXT registration failed: %s", esp_err_to_name(ret));
    sd_set_state(SD_STATE_INIT_FAIL);
    return ret;
  }

  ret = sd_extcs_mount_card(MOUNT_POINT, 5);
  sd_extcs_state_t ext_state = sd_extcs_get_state();
  if (ret == ESP_OK) {
    card = sd_extcs_get_card_handle();
    sd_set_state(SD_STATE_INIT_OK);
  } else if (ext_state == SD_EXTCS_STATE_ABSENT) {
    sd_set_state(SD_STATE_ABSENT);
  } else if (ext_state == SD_EXTCS_STATE_INIT_FAIL) {
    sd_set_state(SD_STATE_INIT_FAIL);
  } else {
    sd_set_state(SD_STATE_MOUNT_FAIL);
  }
  ESP_LOGI(TAG, "SD init result: state=%s extcs=%s ret=%s",
           sd_state_str(s_state), sd_extcs_state_str(ext_state),
           esp_err_to_name(ret));
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
    sd_set_state(SD_STATE_UNINITIALIZED);
  }
  return ret;
}

esp_err_t sd_card_retry_mount(void) {
  if (card) {
    sd_mmc_unmount();
  }
  return sd_card_init();
}

esp_err_t sd_retry_mount(void) { return sd_card_retry_mount(); }

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

sd_state_t sd_get_state(void) { return s_state; }

const char *sd_state_str(sd_state_t state) {
  switch (state) {
  case SD_STATE_INIT_OK:
    return "INIT_OK";
  case SD_STATE_ABSENT:
    return "ABSENT";
  case SD_STATE_INIT_FAIL:
    return "INIT_FAIL";
  case SD_STATE_MOUNT_FAIL:
    return "MOUNT_FAIL";
  default:
    return "UNINITIALIZED";
  }
}

esp_err_t sd_card_self_test(void) {
  if (s_state != SD_STATE_INIT_OK || !card) {
    ESP_LOGW(TAG, "Self-test skipped: SD not mounted (state=%s)",
             sd_state_str(s_state));
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "SD self-test: card info, ls, R/W test");
  sdmmc_card_print_info(stdout, card);

  DIR *dir = opendir(MOUNT_POINT);
  if (!dir) {
    ESP_LOGE(TAG, "Self-test: failed to open %s", MOUNT_POINT);
    return ESP_FAIL;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    ESP_LOGI(TAG, "Self-test: found %s", entry->d_name);
  }
  closedir(dir);

  const char *test_path = MOUNT_POINT "/hello.txt";
  const char *payload = "sd-self-test";

  FILE *f = fopen(test_path, "w");
  if (!f) {
    ESP_LOGE(TAG, "Self-test: write open failed");
    return ESP_FAIL;
  }
  fwrite(payload, 1, strlen(payload), f);
  fclose(f);

  char buf[32] = {0};
  f = fopen(test_path, "r");
  if (!f) {
    ESP_LOGE(TAG, "Self-test: read open failed");
    return ESP_FAIL;
  }
  size_t r = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  unlink(test_path);

  if (r != strlen(payload) || strncmp(buf, payload, strlen(payload)) != 0) {
    ESP_LOGE(TAG, "Self-test: data mismatch (got=%s)", buf);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "SD self-test PASS");
  return ESP_OK;
}
