#include "sd_host_extcs.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "io_extension.h"
#include "sd.h"
#include "sdkconfig.h" // Crucial for CONFIG_ARS defines
#include <inttypes.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifndef CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ 200
#endif

#ifndef CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ 20000
#endif

#ifndef CONFIG_ARS_SD_EXTCS_DEBUG_CMD0
#define CONFIG_ARS_SD_EXTCS_DEBUG_CMD0 0
#endif

#define SD_EXTCS_INIT_FREQ_KHZ CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define SD_EXTCS_TARGET_FREQ_KHZ CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define SD_EXTCS_CMD_TIMEOUT_TICKS pdMS_TO_TICKS(150)
#define SD_EXTCS_CMD0_RETRIES 5
#define SD_EXTCS_CMD0_BACKOFF_MS 8
#define SD_EXTCS_CMD0_RESP_WINDOW_BYTES 16
#define SD_EXTCS_CMD0_CS_SETUP_US 80
#define SD_EXTCS_CS_LOCK_TIMEOUT pdMS_TO_TICKS(200)

static const char *TAG = "sd_extcs";

// --- Internal State ---
static spi_host_device_t s_host_id = SPI2_HOST;
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static spi_device_handle_t s_cleanup_handle = NULL;
static uint32_t s_active_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;
static bool s_ioext_ready = false;
static int s_cs_level = -1; // -1 = unknown, 0 = asserted (LOW), 1 = deasserted (HIGH)
static i2c_master_dev_handle_t s_ioext_handle = NULL;
static SemaphoreHandle_t s_sd_mutex = NULL;
static sd_extcs_state_t s_extcs_state = SD_EXTCS_STATE_UNINITIALIZED;

// Pointer to original implementation
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks);
static esp_err_t sd_extcs_low_speed_init(void);
static esp_err_t sd_extcs_probe_cs_line(void);
static esp_err_t sd_extcs_reset_and_cmd0(bool *card_idle, bool *saw_non_ff);
static inline bool sd_extcs_lock(void);
static inline void sd_extcs_unlock(void);
const char *sd_extcs_state_str(sd_extcs_state_t state);

// --- GPIO Helpers ---
static esp_err_t sd_extcs_ensure_mutex(void) {
  if (!s_sd_mutex) {
    s_sd_mutex = xSemaphoreCreateRecursiveMutex();
  }
  return s_sd_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static inline bool sd_extcs_lock(void) {
  if (sd_extcs_ensure_mutex() != ESP_OK)
    return false;
  return xSemaphoreTakeRecursive(s_sd_mutex, SD_EXTCS_CS_LOCK_TIMEOUT) == pdTRUE;
}

static inline void sd_extcs_unlock(void) {
  if (s_sd_mutex)
    xSemaphoreGiveRecursive(s_sd_mutex);
}

const char *sd_extcs_state_str(sd_extcs_state_t state) {
  switch (state) {
  case SD_EXTCS_STATE_IDLE_READY:
    return "IDLE_READY";
  case SD_EXTCS_STATE_ABSENT:
    return "ABSENT";
  case SD_EXTCS_STATE_INIT_FAIL:
    return "INIT_FAIL";
  default:
    return "UNINITIALIZED";
  }
}

// --- GPIO Helpers ---
static esp_err_t sd_extcs_set_cs(bool asserted) {
  if (!s_ioext_ready || s_ioext_handle == NULL) {
    ESP_LOGE(TAG, "CS %s failed: IO expander not ready", asserted ? "assert" :
                                                           "deassert");
    return ESP_ERR_INVALID_STATE;
  }

  // IO4: 0 = Low (Assert), 1 = High (Deassert)
  const int desired_level = asserted ? 0 : 1;
  if (s_cs_level == desired_level) {
    return ESP_OK; // Skip redundant I2C write
  }

  esp_err_t err = IO_EXTENSION_Output(IO_EXTENSION_IO_4, desired_level);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS %s failed: %s", asserted ? "assert" : "deassert",
             esp_err_to_name(err));
  } else {
    s_cs_level = desired_level;
    ESP_LOGD(TAG, "CS=%s via IOEXT4", asserted ? "LOW" : "HIGH");
  }
  return err;
}

static void sd_extcs_send_dummy_clocks(size_t byte_count) {
  if (!s_cleanup_handle)
    return;
  if (!sd_extcs_lock())
    return;

  uint8_t dummy = 0xFF;
  for (size_t i = 0; i < byte_count; ++i) {
    spi_transaction_t t_cleanup = {
        .length = 8,
        .tx_buffer = &dummy,
        .rx_buffer = NULL,
    };
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }
  sd_extcs_unlock();
}

static esp_err_t sd_extcs_probe_cs_line(void) {
  // Toggle CS via the IO extender and read back level to ensure the IO path is
  // alive. This catches the "CS stuck high" hardware fault that makes CMD0
  // always return 0xFF.
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  esp_err_t err = IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CS probe: failed to set high: %s", esp_err_to_name(err));
    sd_extcs_unlock();
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(2));
  uint8_t level_high = IO_EXTENSION_Input(IO_EXTENSION_IO_4);

  err = IO_EXTENSION_Output(IO_EXTENSION_IO_4, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CS probe: failed to set low: %s", esp_err_to_name(err));
    sd_extcs_unlock();
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(2));
  uint8_t level_low = IO_EXTENSION_Input(IO_EXTENSION_IO_4);

  // Restore idle high
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);
  sd_extcs_unlock();

  if (level_high == level_low) {
    ESP_LOGE(TAG,
             "CS probe: IOEXT4 not toggling (high=%u low=%u). Check wiring/3V3.",
             level_high, level_low);
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "CS probe: IOEXT4 toggled (high=%u low=%u)", level_high,
           level_low);
  return ESP_OK;
}

static esp_err_t sd_extcs_wait_for_response(uint8_t *out, TickType_t timeout) {
  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;

  TickType_t start = xTaskGetTickCount();
  uint8_t rx = 0xFF;
  do {
    spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
    esp_err_t err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
    if (err != ESP_OK)
      return err;

    rx = t_rx.rx_data[0];
    if (rx != 0xFF)
      break;
  } while ((xTaskGetTickCount() - start) < timeout);

  *out = rx;
  return (rx == 0xFF) ? ESP_ERR_TIMEOUT : ESP_OK;
}

static esp_err_t sd_extcs_reset_and_cmd0(bool *card_idle, bool *saw_non_ff) {
  if (card_idle)
    *card_idle = false;
  if (saw_non_ff)
    *saw_non_ff = false;

  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  // Force SPI mode: CS high then >=80 clocks
  sd_extcs_set_cs(false);
  vTaskDelay(pdMS_TO_TICKS(2));
  sd_extcs_send_dummy_clocks(10);

  bool idle_seen = false;
  bool saw_data = false;
  esp_err_t last_err = ESP_ERR_TIMEOUT;

  for (int attempt = 0; attempt < SD_EXTCS_CMD0_RETRIES; ++attempt) {
    uint8_t frame[6] = {0x40 | 0, 0, 0, 0, 0, 0x95};
    uint8_t raw[SD_EXTCS_CMD0_RESP_WINDOW_BYTES] = {0};
    size_t raw_len = 0;
    bool valid_r1 = false;
    uint8_t r1 = 0xFF;
    bool invalid_bit7 = false;

    esp_err_t err = sd_extcs_set_cs(true);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "CMD0 try %d/%d: CS assert failed: %s", attempt + 1,
               SD_EXTCS_CMD0_RETRIES, esp_err_to_name(err));
      last_err = err;
      sd_extcs_unlock();
      return err;
    }

    ets_delay_us(SD_EXTCS_CMD0_CS_SETUP_US);

    spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
    err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD0 try %d/%d TX failed: %s", attempt + 1,
               SD_EXTCS_CMD0_RETRIES, esp_err_to_name(err));
      last_err = err;
      sd_extcs_set_cs(false);
      sd_extcs_send_dummy_clocks(1);
      vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CMD0_BACKOFF_MS));
      continue;
    }

    for (size_t i = 0; i < SD_EXTCS_CMD0_RESP_WINDOW_BYTES; ++i) {
      spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
      err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      if (err != ESP_OK)
        break;

      uint8_t byte = t_rx.rx_data[0];
      raw[raw_len++] = byte;
      if (byte != 0xFF)
        saw_data = true;
      if ((byte & 0x80) != 0) {
        invalid_bit7 = true;
        continue; // Not a valid R1 byte
      }
      r1 = byte;
      valid_r1 = true;
      break;
    }

    sd_extcs_send_dummy_clocks(1);
    sd_extcs_set_cs(false);

    char dump[SD_EXTCS_CMD0_RESP_WINDOW_BYTES * 3 + 1];
    size_t idx = 0;
    for (size_t i = 0; i < raw_len; ++i) {
      idx += snprintf(dump + idx, sizeof(dump) - idx, " %02X", raw[i]);
      if (idx >= sizeof(dump))
        break;
    }
    dump[sizeof(dump) - 1] = '\0';

    char result_str[48];
    if (valid_r1) {
      snprintf(result_str, sizeof(result_str), "R1=0x%02X (%s)%s", r1,
               r1 == 0x01 ? "idle" : "not idle",
               invalid_bit7 ? " invalid-bit7" : "");
    } else if (invalid_bit7) {
      snprintf(result_str, sizeof(result_str), "invalid-bit7 raw=%s",
               idx ? dump : "<ff>");
    } else {
      snprintf(result_str, sizeof(result_str), "timeout raw=%s",
               idx ? dump : "<ff>");
    }

    if (CONFIG_ARS_SD_EXTCS_DEBUG_CMD0) {
      ESP_LOGI(TAG,
               "CMD0 try %d/%d @%u kHz cs_setup=%u us raw[%zu]=%s -> %s",
               attempt + 1, SD_EXTCS_CMD0_RETRIES, s_active_freq_khz,
               SD_EXTCS_CMD0_CS_SETUP_US, raw_len, idx ? dump : " <none>",
               result_str);
    } else {
      ESP_LOGI(TAG, "CMD0 try %d/%d -> %s", attempt + 1,
               SD_EXTCS_CMD0_RETRIES, result_str);
    }

    if (valid_r1 && r1 == 0x01) {
      idle_seen = true;
      last_err = ESP_OK;
      break;
    }

    last_err = valid_r1 ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
    vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CMD0_BACKOFF_MS));
  }

  if (card_idle)
    *card_idle = idle_seen;
  if (saw_non_ff)
    *saw_non_ff = saw_data;

  sd_extcs_unlock();
  return last_err;
}

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks) {
  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  uint8_t frame[6];
  frame[0] = 0x40 | (cmd & 0x3F);
  frame[1] = (arg >> 24) & 0xFF;
  frame[2] = (arg >> 16) & 0xFF;
  frame[3] = (arg >> 8) & 0xFF;
  frame[4] = arg & 0xFF;
  frame[5] = crc | 0x01; // end bit required

  esp_err_t err = sd_extcs_set_cs(true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS Assert failed before CMD%u: %s", cmd,
             esp_err_to_name(err));
    sd_extcs_unlock();
    return err;
  }

  ets_delay_us(5);

  spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
  err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u TX failed: %s", cmd, esp_err_to_name(err));
    sd_extcs_set_cs(false);
    sd_extcs_send_dummy_clocks(1);
    sd_extcs_unlock();
    return err;
  }

  uint8_t first_byte = 0xFF;
  err = sd_extcs_wait_for_response(&first_byte, timeout_ticks);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u timeout waiting R1", cmd);
  }

  ESP_LOGD(TAG, "CMD%u resp0=0x%02X (len=%zu)", cmd, first_byte,
           response_len);

  if (response && response_len > 0) {
    response[0] = first_byte;
    for (size_t i = 1; i < response_len; ++i) {
      spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
      esp_err_t rx_err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      if (rx_err != ESP_OK) {
        err = rx_err;
        break;
      }
      response[i] = t_rx.rx_data[0];
    }
  }

  sd_extcs_set_cs(false);
  sd_extcs_send_dummy_clocks(1);
  ets_delay_us(2);

  sd_extcs_unlock();

  return err;
}

// --- Transaction Wrapper ---
static esp_err_t sd_extcs_do_transaction(int slot, sdmmc_command_t *cmdinfo) {
#if CONFIG_ARS_SD_EXTCS_TIMING_LOG
  int64_t t_start_us = esp_timer_get_time();
#endif

  // 1. Assert CS
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  esp_err_t ret = sd_extcs_set_cs(true);
  if (ret != ESP_OK) {
    // If CS fails (e.g. I2C timeout), we can't trust the transaction.
    // But we must return an error compatible with storage stack.
    ESP_LOGW(TAG, "CS Assert Failed: %s", esp_err_to_name(ret));
    sd_extcs_unlock();
    return ESP_ERR_TIMEOUT;
  }

  // Explicit setup time for manual CS
  ets_delay_us(4);

  // 2. Delegate to standard SDSPI implementation
  ret = s_original_do_transaction(slot, cmdinfo);

  // 3. Deassert CS
  sd_extcs_set_cs(false); // Best effort deassert

  // 4. Dummy Clocks (8 cycles)
  if (s_cleanup_handle) {
    // Using polling to be quick and synchronous
    uint8_t dummy = 0xFF;
    spi_transaction_t t_cleanup = {
        .length = 8, .tx_buffer = &dummy, .rx_buffer = NULL};
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }
  ets_delay_us(2);

#if CONFIG_ARS_SD_EXTCS_TIMING_LOG
  const uint8_t opcode = cmdinfo ? cmdinfo->opcode : 0xFF;
  const int64_t duration_us = esp_timer_get_time() - t_start_us;
  ESP_LOGI(TAG,
           "CMD%u done in %" PRId64
           " us (len=%d, freq=%u kHz, ret=%s)",
           opcode, duration_us, cmdinfo ? cmdinfo->datalen : 0,
           s_active_freq_khz, esp_err_to_name(ret));
#endif

  sd_extcs_unlock();

  return ret;
}

static esp_err_t sd_extcs_low_speed_init(void) {
  gpio_set_pull_mode(CONFIG_ARS_SD_MISO, GPIO_PULLUP_ONLY);

  s_active_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;
  ESP_LOGI(TAG, "Low-speed init at %u kHz", s_active_freq_khz);

  bool card_idle = false;
  bool saw_non_ff = false;
  esp_err_t err = sd_extcs_reset_and_cmd0(&card_idle, &saw_non_ff);
  if (!card_idle) {
    s_extcs_state = saw_non_ff ? SD_EXTCS_STATE_INIT_FAIL
                               : SD_EXTCS_STATE_ABSENT;
    ESP_LOGW(TAG, "CMD0 failed: state=%s err=%s (saw_non_ff=%d)",
             sd_extcs_state_str(s_extcs_state), esp_err_to_name(err),
             saw_non_ff);
    return saw_non_ff ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_NOT_FOUND;
  }

  s_extcs_state = SD_EXTCS_STATE_IDLE_READY;
  ESP_LOGI(TAG, "CMD0: Card entered idle state (R1=0x01)");

  uint8_t resp_r1 = 0xFF;

  // CMD8: check voltage range
  uint8_t resp_r7[5] = {0};
  err = sd_extcs_send_command(8, 0x000001AA, 0x87, resp_r7, sizeof(resp_r7),
                              SD_EXTCS_CMD_TIMEOUT_TICKS);
  bool sdhc_candidate = false;
  if (err == ESP_OK && resp_r7[0] == 0x01) {
    uint32_t pattern = ((uint32_t)resp_r7[1] << 24) |
                       ((uint32_t)resp_r7[2] << 16) |
                       ((uint32_t)resp_r7[3] << 8) | resp_r7[4];
    sdhc_candidate = (pattern == 0x000001AA);
    ESP_LOGI(TAG, "CMD8 OK (pattern=0x%08" PRIX32 ")", pattern);
  } else {
    ESP_LOGW(TAG,
             "CMD8 failed/illegal (resp=0x%02X). Assuming SDSC or older card.",
             resp_r7[0]);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD8 error: %s. Insert SD card or verify 3.3V wiring.",
               esp_err_to_name(err));
    }
  }

  // ACMD41 loop with HCS if supported
  const uint32_t acmd41_arg = sdhc_candidate ? 0x40000000 : 0x00000000;
  bool card_ready = false;
  int acmd41_attempts = 0;
  for (int i = 0; i < 200; ++i) {
    acmd41_attempts = i + 1;
    err = sd_extcs_send_command(55, 0x00000000, 0x65, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD55 failed (attempt %d): %s", i + 1,
               esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    err = sd_extcs_send_command(41, acmd41_arg, 0x77, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err == ESP_OK && resp_r1 == 0x00) {
      card_ready = true;
      break;
    }

    ESP_LOGI(TAG, "ACMD41 attempt %d resp=0x%02X err=%s", i + 1, resp_r1,
             esp_err_to_name(err));

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!card_ready) {
    ESP_LOGE(TAG,
             "ACMD41 timeout. SD card not ready. Insert card or verify cabling.");
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    return ESP_ERR_TIMEOUT;
  }
  ESP_LOGI(TAG, "ACMD41 completed in %d attempt(s), card ready", acmd41_attempts);

  // CMD58: read OCR
  uint8_t resp_r3[5] = {0};
  err = sd_extcs_send_command(58, 0x00000000, 0xFD, resp_r3, sizeof(resp_r3),
                              SD_EXTCS_CMD_TIMEOUT_TICKS);
  if (err != ESP_OK || resp_r3[0] != 0x00) {
    ESP_LOGE(TAG,
             "CMD58 failed (resp=0x%02X). Insert SD card or check wiring.",
             resp_r3[0]);
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    return ESP_ERR_TIMEOUT;
  }

  uint32_t ocr = ((uint32_t)resp_r3[1] << 24) | ((uint32_t)resp_r3[2] << 16) |
                 ((uint32_t)resp_r3[3] << 8) | resp_r3[4];
  bool high_capacity = (ocr & (1 << 30)) != 0;
  ESP_LOGI(TAG, "OCR=0x%08" PRIX32 " (CCS=%d)", ocr, high_capacity);

  return ESP_OK;
}

// --- Public API ---

esp_err_t sd_extcs_mount_card(const char *mount_point, size_t max_files) {
  if (s_mounted)
    return ESP_OK;

  esp_err_t ret;

  ESP_LOGI(TAG, "Mounting SD (SDSPI ext-CS) init=%u kHz target=%u kHz",\
           SD_EXTCS_INIT_FREQ_KHZ, SD_EXTCS_TARGET_FREQ_KHZ);

  s_extcs_state = SD_EXTCS_STATE_UNINITIALIZED;

#if CONFIG_ARS_SD_SDMMC_DEBUG_LOG
  esp_log_level_set("sdspi_host", ESP_LOG_DEBUG);
  esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
  esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
#endif

  // 1. IO Init (Safe-Fail)
  sd_extcs_ensure_mutex();
  if (!s_ioext_ready) {
    if (IO_EXTENSION_Is_Initialized()) {
      s_ioext_handle = IO_EXTENSION_Get_Handle();
      s_ioext_ready = s_ioext_handle != NULL;
    }
  }
  if (!s_ioext_ready) {
    ESP_LOGE(TAG, "SD: ExtCS Mode unavailable (IOEXT not initialized)");
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    return ESP_ERR_INVALID_STATE;
  }
  IO_EXTENSION_IO_Mode(0xFF); // ensure outputs (push-pull)
  s_cs_level = -1;
  if (sd_extcs_lock()) {
    sd_extcs_set_cs(false);
    sd_extcs_unlock();
  } else {
    return ESP_ERR_TIMEOUT;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  ret = sd_extcs_probe_cs_line();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SD: CS line check failed. Card will not respond until fixed.");
    return ret;
  }

  // 2. SPI Bus Init
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = CONFIG_ARS_SD_MOSI,
      .miso_io_num = CONFIG_ARS_SD_MISO,
      .sclk_io_num = CONFIG_ARS_SD_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  ret = spi_bus_initialize(s_host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "SPI Bus Init Failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "SDSPI pins: MISO=%d MOSI=%d SCK=%d CS=IOEXT%d (active-low)",
           CONFIG_ARS_SD_MISO, CONFIG_ARS_SD_MOSI, CONFIG_ARS_SD_SCK,
           IO_EXTENSION_IO_4);

  // 3. Cleanup Device
  if (!s_cleanup_handle) {
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .clock_speed_hz = SD_EXTCS_INIT_FREQ_KHZ * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(s_host_id, &dev_cfg, &s_cleanup_handle);
    if (ret != ESP_OK)
      return ret;
  }

  // 4. Strict low-speed init sequence
  ret = sd_extcs_low_speed_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Pre-init failed. Card may be absent or wiring is wrong.");
    return ret;
  }

  // 5. Host Config
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = s_host_id;
  host.max_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;

  s_original_do_transaction = host.do_transaction;
  host.do_transaction = sd_extcs_do_transaction;

  // 6. Slot Config (Disable CS)
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.host_id = s_host_id;
  slot_config.gpio_cs = -1;

  // 7. Mount
  esp_vfs_fat_mount_config_t mount_config = {.format_if_mount_failed = false,
                                             .max_files = max_files,
                                             .allocation_unit_size = 16 * 1024};

  ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config,
                                &s_card);

  if (ret == ESP_OK) {
    s_mounted = true;
    s_active_freq_khz = host.max_freq_khz;
    // Verify user can read simple stats
    sdmmc_card_print_info(stdout, s_card);

    // Increase clock after OCR stable and card enumerated
    uint32_t target_khz = SD_EXTCS_TARGET_FREQ_KHZ;
    if (s_card && s_card->max_freq_khz > 0 && s_card->max_freq_khz < target_khz)
      target_khz = s_card->max_freq_khz;

    esp_err_t clk_ret = sdspi_host_set_card_clk(s_host_id, target_khz);
    if (clk_ret == ESP_OK) {
      ESP_LOGI(TAG, "Raised SD SPI clock to %u kHz", target_khz);
      s_active_freq_khz = target_khz;
    } else {
      ESP_LOGW(TAG, "Failed to raise SD SPI clock: %s", esp_err_to_name(clk_ret));
    }
  } else {
    ESP_LOGE(TAG, "Mount Failed: %s. Insert SD card or verify wiring.",
             esp_err_to_name(ret));
    s_card = NULL;
    if (s_extcs_state == SD_EXTCS_STATE_IDLE_READY)
      s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
  }

  return ret;
}

esp_err_t sd_extcs_unmount_card(const char *mount_point) {
  if (!s_mounted)
    return ESP_OK;

  esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, s_card);
  if (ret == ESP_OK) {
    s_card = NULL;
    s_mounted = false;
    s_cs_level = -1;

    // Remove cleanup device
    if (s_cleanup_handle) {
      spi_bus_remove_device(s_cleanup_handle);
      s_cleanup_handle = NULL;
    }

    // Note: We do NOT free the SPI bus because other devices (LCD?) might
    // technically use it But here SPI2_HOST is exclusively for SD in most
    // setups. If we want to free: spi_bus_free(s_host_id);
  }
  return ret;
}

sdmmc_card_t *sd_extcs_get_card_handle(void) { return s_card; }

esp_err_t sd_extcs_register_io_extender(i2c_master_dev_handle_t handle) {
  if (handle == NULL)
    return ESP_ERR_INVALID_ARG;
  s_ioext_handle = handle;
  s_ioext_ready = true;
  return sd_extcs_ensure_mutex();
}

sd_extcs_state_t sd_extcs_get_state(void) { return s_extcs_state; }
