#include "sd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_host_extcs.h"
#include "sdkconfig.h"
#include "touch.h"
#include "sdmmc_cmd.h"
#include <dirent.h>
#include <string.h>

static const char *TAG = "sd";

// Global handle
sdmmc_card_t *card = NULL;
static sd_state_t s_state = SD_STATE_UNINITIALIZED;
static SemaphoreHandle_t s_sd_mutex = NULL;
static bool s_retry_in_progress = false;
static TickType_t s_last_retry_tick = 0;

#define SD_RETRY_MIN_BACKOFF_MS 2000
#define SD_INIT_DEADLINE_US 1200000

static void sd_set_state(sd_state_t new_state) {
  if (s_state != new_state) {
    ESP_LOGI(TAG, "SD state -> %s", sd_state_str(new_state));
  }
  s_state = new_state;
}

static bool sd_lock(TickType_t ticks) {
  if (!s_sd_mutex) {
    s_sd_mutex = xSemaphoreCreateRecursiveMutex();
  }
  if (!s_sd_mutex)
    return false;
  return xSemaphoreTakeRecursive(s_sd_mutex, ticks) == pdTRUE;
}

static void sd_unlock(void) {
  if (s_sd_mutex) {
    xSemaphoreGiveRecursive(s_sd_mutex);
  }
}

esp_err_t sd_card_init() {
  ESP_LOGI(TAG, "Initializing SD (ExtCS Mode)...");

  if (!sd_lock(pdMS_TO_TICKS(50))) {
    ESP_LOGW(TAG, "SD init skipped: busy");
    return ESP_ERR_TIMEOUT;
  }

  bool touch_paused = false;
#if CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT
  touch_pause_for_sd_init(true);
  touch_paused = true;
#endif

  sd_set_state(SD_STATE_UNINITIALIZED);
  card = NULL;

  esp_err_t ret = sd_extcs_register_io_extender(IO_EXTENSION_Get_Handle());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SD IOEXT registration failed: %s", esp_err_to_name(ret));
    sd_set_state(SD_STATE_INIT_FAIL);
    sd_unlock();
    if (touch_paused)
      touch_pause_for_sd_init(false);
    return ret;
  }

  const int max_attempts = 2;
  sd_extcs_state_t ext_state = SD_EXTCS_STATE_UNINITIALIZED;
  int64_t deadline_us = esp_timer_get_time() + SD_INIT_DEADLINE_US;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    if (esp_timer_get_time() > deadline_us) {
      ESP_LOGW(TAG, "SD init deadline reached before attempt %d", attempt + 1);
      ret = ESP_ERR_TIMEOUT;
      break;
    }
    ret = sd_extcs_mount_card(MOUNT_POINT, 5);
    ext_state = sd_extcs_get_state();
    if (ret == ESP_OK || ext_state == SD_EXTCS_STATE_ABSENT) {
      break;
    }
    ESP_LOGW(TAG, "SD mount attempt %d failed: %s (state=%s)", attempt + 1,
             esp_err_to_name(ret), sd_extcs_state_str(ext_state));
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (ret == ESP_OK) {
    card = sd_extcs_get_card_handle();

    // ARS FIX (C): Validate SD card with a minimal read test BEFORE declaring
    // INIT_OK. This prevents false INIT_OK when mount succeeds but DMA reads
    // fail (error 0x107).
    bool read_validated = false;
    if (card) {
      // Try to read sector 0 (MBR/boot sector) as a sanity check
      uint8_t test_buf[512] __attribute__((aligned(4)));
      esp_err_t read_ret = sdmmc_read_sectors(card, test_buf, 0, 1);
      if (read_ret == ESP_OK) {
        // Basic sanity: check for valid boot signature (0x55AA at offset 510)
        // or non-zero data (indicates card is readable)
        bool has_data = false;
        for (int i = 0; i < 16 && !has_data; i++) {
          if (test_buf[i] != 0x00 && test_buf[i] != 0xFF) {
            has_data = true;
          }
        }
        if (has_data || (test_buf[510] == 0x55 && test_buf[511] == 0xAA)) {
          read_validated = true;
          ESP_LOGI(TAG, "SD read validation PASS (sector 0 readable)");
        } else {
          ESP_LOGW(TAG,
                   "SD read validation: sector 0 appears empty/unformatted");
          read_validated = true; // Still OK, just unformatted
        }
      } else {
        ESP_LOGE(TAG, "SD read validation FAILED: %s (0x%x)",
                 esp_err_to_name(read_ret), read_ret);
      }
    }

    if (read_validated) {
      sd_set_state(SD_STATE_INIT_OK);
    } else {
      ESP_LOGE(TAG, "SD mounted but read validation failed; state=MOUNT_FAIL");
      sd_set_state(SD_STATE_MOUNT_FAIL);
      ret = ESP_ERR_INVALID_RESPONSE;
    }
  } else if (ext_state == SD_EXTCS_STATE_ABSENT) {
    sd_set_state(SD_STATE_ABSENT);
    ESP_LOGW(TAG, "SD init: NO_CARD detected (ext-CS path healthy)");
  } else if (ext_state == SD_EXTCS_STATE_INIT_FAIL) {
    sd_set_state(SD_STATE_INIT_FAIL);
  } else {
    sd_set_state(SD_STATE_MOUNT_FAIL);
  }
  sd_extcs_sequence_stats_t seq = {0};
  if (sd_extcs_get_sequence_stats(&seq) == ESP_OK) {
    ESP_LOGI(TAG,
             "SD pipeline: pre_clks=%uB cmd0=%d cmd8=%d acmd41=%d cmd58=%d "
             "init=%u kHz target=%u kHz final=%s",
           seq.pre_clks_bytes, seq.cmd0_seen, seq.cmd8_seen, seq.acmd41_seen,
           seq.cmd58_seen, seq.init_freq_khz, seq.target_freq_khz,
           sd_extcs_state_str(seq.final_state));
  }
  ESP_LOGI(TAG, "SD init result: state=%s extcs=%s ret=%s",
           sd_state_str(s_state), sd_extcs_state_str(ext_state),
           esp_err_to_name(ret));
  sd_unlock();
  if (touch_paused)
    touch_pause_for_sd_init(false);
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
  if (!sd_lock(pdMS_TO_TICKS(20))) {
    ESP_LOGW(TAG, "SD retry skipped: busy");
    return ESP_ERR_TIMEOUT;
  }
  if (s_retry_in_progress) {
    sd_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  TickType_t now = xTaskGetTickCount();
  if (s_last_retry_tick != 0 &&
      (now - s_last_retry_tick) < pdMS_TO_TICKS(SD_RETRY_MIN_BACKOFF_MS)) {
    sd_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  s_retry_in_progress = true;
  sd_unlock();

  esp_err_t ret = ESP_OK;
  if (card) {
    sd_mmc_unmount();
  }
  ret = sd_card_init();

  if (sd_lock(pdMS_TO_TICKS(50))) {
    s_last_retry_tick = xTaskGetTickCount();
    s_retry_in_progress = false;
    sd_unlock();
  } else {
    s_retry_in_progress = false;
  }
  return ret;
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
