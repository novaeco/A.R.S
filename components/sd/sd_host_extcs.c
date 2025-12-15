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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static esp_err_t sd_extcs_configure_cleanup_device(uint32_t freq_khz);

#ifndef SD_EXTCS_CS_I2C_SETTLE_US
#define SD_EXTCS_CS_I2C_SETTLE_US SD_EXTCS_CS_ASSERT_SETTLE_US
#endif

#ifndef SD_EXTCS_CMD58_RETRIES
#define SD_EXTCS_CMD58_RETRIES SD_EXTCS_CMD0_RETRIES
#endif

#ifndef CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ 200
#endif

#ifndef CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ 10000
#endif

#ifndef CONFIG_ARS_SD_EXTCS_RECOVERY_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_RECOVERY_FREQ_KHZ 8000
#endif

#ifndef CONFIG_ARS_SD_EXTCS_SAFE_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_SAFE_FREQ_KHZ 4000
#endif

#ifndef CONFIG_ARS_SD_EXTCS_DEBUG_CMD0
#define CONFIG_ARS_SD_EXTCS_DEBUG_CMD0 0
#endif

#ifndef CONFIG_ARS_SD_EXTCS_CMD0_PRE_CLKS_BYTES
#define CONFIG_ARS_SD_EXTCS_CMD0_PRE_CLKS_BYTES 20
#endif

#ifndef CONFIG_ARS_SD_EXTCS_CS_POST_TOGGLE_DELAY_MS
#define CONFIG_ARS_SD_EXTCS_CS_POST_TOGGLE_DELAY_MS 1
#endif

#ifndef CONFIG_ARS_SD_EXTCS_CS_I2C_SETTLE_US
#define CONFIG_ARS_SD_EXTCS_CS_I2C_SETTLE_US 50
#endif

#ifndef CONFIG_ARS_SD_EXTCS_CMD58_RETRIES
#define CONFIG_ARS_SD_EXTCS_CMD58_RETRIES 5
#endif

#ifndef CONFIG_ARS_SD_EXTCS_CS_PRE_CMD0_DELAY_US
#define CONFIG_ARS_SD_EXTCS_CS_PRE_CMD0_DELAY_US 0
#endif

#define SD_EXTCS_INIT_FREQ_KHZ CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define SD_EXTCS_TARGET_FREQ_KHZ CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define SD_EXTCS_CMD0_PRE_CLKS_BYTES CONFIG_ARS_SD_EXTCS_CMD0_PRE_CLKS_BYTES
#define SD_EXTCS_CS_POST_TOGGLE_DELAY_MS CONFIG_ARS_SD_EXTCS_CS_POST_TOGGLE_DELAY_MS
#define SD_EXTCS_CS_PRE_CMD0_DELAY_US CONFIG_ARS_SD_EXTCS_CS_PRE_CMD0_DELAY_US
#define SD_EXTCS_CMD_TIMEOUT_TICKS pdMS_TO_TICKS(150)
#define SD_EXTCS_CMD0_RETRIES 12
#define SD_EXTCS_CMD0_BACKOFF_MS 8
#define SD_EXTCS_CMD0_RESP_WINDOW_BYTES 64
#define SD_EXTCS_CMD0_EXTRA_IDLE_CLKS_BYTES 8
#define SD_EXTCS_CS_ASSERT_SETTLE_US 120
#define SD_EXTCS_CS_DEASSERT_SETTLE_US 40
#define SD_EXTCS_CMD0_PRE_CMD_DELAY_US 240
#define SD_EXTCS_CS_LOCK_TIMEOUT pdMS_TO_TICKS(200)
#define SD_EXTCS_CMD0_RAW_STR_LIMIT (SD_EXTCS_CMD0_RESP_WINDOW_BYTES * 3)
#define SD_EXTCS_CMD0_RESULT_STR_LIMIT 192
#define SD_EXTCS_CMD0_SLOW_FREQ_KHZ 100
#define SD_EXTCS_R1_BITS_MAX 48
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

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
static bool s_warned_cs_low_stuck = false;
static bool s_cmd0_diag_logged = false;
static uint8_t s_last_cs_latched = 0xFF;
static uint8_t s_last_cs_input = 0xFF;

typedef struct {
  uint8_t sample_high[4];
  uint8_t sample_low[4];
  bool high_all_ff;
  bool low_all_ff;
} sd_extcs_miso_diag_t;

// Pointer to original implementation
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks);
static esp_err_t sd_extcs_low_speed_init(void);
static esp_err_t sd_extcs_probe_cs_line(void);
static esp_err_t sd_extcs_reset_and_cmd0(bool *card_idle, bool *saw_non_ff,
                                         bool *cs_low_stuck_warning,
                                         sd_extcs_miso_diag_t *miso_diag);
static bool sd_extcs_check_miso_health(bool *cs_low_all_ff,
                                       sd_extcs_miso_diag_t *miso_diag);
static inline bool sd_extcs_lock(void);
static inline void sd_extcs_unlock(void);
static size_t sd_extcs_decode_r1(uint8_t r1, char *buf, size_t len);
static size_t sd_extcs_safe_hex_dump(const uint8_t *src, size_t src_len, char *dst,
                                     size_t dst_size, bool leading_space);
static size_t sd_extcs_strnlen_cap(const char *s, size_t max_len);
// GCC 12+ aggressively warns about potential truncation; use a bounded helper to
// force null-termination and placate -Wformat-truncation without weakening
// warnings globally.
static size_t sd_extcs_safe_snprintf(char *dst, size_t dst_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
// Avoids -Waddress false positives when using stack buffers that are never NULL.
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

static size_t sd_extcs_strnlen_cap(const char *s, size_t max_len) {
  if (!s || max_len == 0)
    return 0;
  size_t n = strnlen(s, max_len);
  return n > max_len ? max_len : n;
}

static size_t sd_extcs_safe_snprintf(char *dst, size_t dst_size, const char *fmt, ...) {
  if (!dst || dst_size == 0)
    return 0;

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(dst, dst_size, fmt, args);
  va_end(args);

  if (written < 0) {
    dst[0] = '\0';
    return 0;
  }
  if ((size_t)written >= dst_size) {
    dst[dst_size - 1] = '\0';
    return dst_size - 1;
  }
  return (size_t)written;
}

static size_t sd_extcs_safe_hex_dump(const uint8_t *src, size_t src_len, char *dst,
                                     size_t dst_size, bool leading_space) {
  if (!dst || dst_size == 0)
    return 0;
  size_t idx = 0;
  dst[0] = '\0';

  for (size_t i = 0; i < src_len; ++i) {
    if (idx >= dst_size - 1)
      break;
    if (leading_space || idx > 0) {
      if (idx >= dst_size - 1)
        break;
      dst[idx++] = ' ';
    }

    uint8_t byte = src[i];
    const char hex_chars[] = "0123456789ABCDEF";
    if (idx + 2 >= dst_size)
      break;
    dst[idx++] = hex_chars[(byte >> 4) & 0x0F];
    dst[idx++] = hex_chars[byte & 0x0F];
  }

  if (idx < dst_size)
    dst[idx] = '\0';
  else
    dst[dst_size - 1] = '\0';
  return idx;
}

static size_t sd_extcs_decode_r1(uint8_t r1, char *buf, size_t len) {
  if (!buf || len == 0)
    return 0;

  return sd_extcs_safe_snprintf(buf, len, "%s%s%s%s%s%s%s",
                                (r1 & 0x01) ? "IDLE" : "READY",
                                (r1 & 0x02) ? " ERASE_RESET" : "",
                                (r1 & 0x04) ? " ILLEGAL_CMD" : "",
                                (r1 & 0x08) ? " CRC_ERR" : "",
                                (r1 & 0x10) ? " ERASE_SEQ" : "",
                                (r1 & 0x20) ? " ADDR_ERR" : "",
                                (r1 & 0x40) ? " PARAM_ERR" : "");
}

static esp_err_t sd_extcs_raise_clock(uint32_t host_target_khz,
                                      uint32_t card_limit_khz,
                                      uint32_t init_khz) {
  const uint32_t candidates[] = {host_target_khz, CONFIG_ARS_SD_EXTCS_RECOVERY_FREQ_KHZ,
                                 CONFIG_ARS_SD_EXTCS_SAFE_FREQ_KHZ};

  esp_err_t last_err = ESP_ERR_INVALID_STATE;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    uint32_t candidate = candidates[i];
    if (candidate == 0)
      continue;

    if (card_limit_khz > 0 && candidate > card_limit_khz)
      candidate = card_limit_khz;
    if (candidate < init_khz)
      candidate = init_khz;
    if (candidate == s_active_freq_khz)
      continue;

    last_err = sdspi_host_set_card_clk(s_host_id, candidate);
    if (last_err == ESP_OK) {
      s_active_freq_khz = candidate;
      sd_extcs_configure_cleanup_device(candidate);
      ESP_LOGI(TAG,
               "Raised SD SPI clock to %u kHz (card limit=%u kHz host target=%u)",
               candidate, card_limit_khz, host_target_khz);
      return ESP_OK;
    }

    ESP_LOGW(TAG, "Clock switch to %u kHz failed: %s", candidate,
             esp_err_to_name(last_err));
  }

  return last_err;
}

// --- GPIO Helpers ---
// Drive SD CS through the CH32V003 IO extender and log the requested level so
// we can distinguish IO path failures from card absence during bring-up.
static esp_err_t sd_extcs_set_cs_level(bool level_high) {
  if (!s_ioext_ready || s_ioext_handle == NULL) {
    ESP_LOGE(TAG, "CS %s failed: IO expander not ready", level_high ? "HIGH" :
                                                           "LOW");
    return ESP_ERR_INVALID_STATE;
  }

  // IO4: 0 = Low (Assert), 1 = High (Deassert)
  const int desired_level = level_high ? 1 : 0;
  if (s_cs_level == desired_level) {
    ESP_LOGD(TAG, "CS already %s (IOEXT4)", level_high ? "HIGH" : "LOW");
    return ESP_OK; // Skip redundant I2C write
  }

  uint8_t latched = 0xFF;
  uint8_t sampled = 0xFF;

  esp_err_t err = IO_EXTENSION_Output_With_Readback(IO_EXTENSION_IO_4,
                                                    desired_level, &latched,
                                                    &sampled);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS->%s failed: %s", level_high ? "HIGH" : "LOW",
             esp_err_to_name(err));
  } else {
    s_cs_level = desired_level;
    s_last_cs_latched = latched;
    s_last_cs_input = sampled;
    ESP_LOGI(TAG,
             "CS->%s via IOEXT4 (latched=%u input=%u result=%s)",
             level_high ? "HIGH" : "LOW", latched, sampled,
             esp_err_to_name(err));
  }

  if (SD_EXTCS_CS_POST_TOGGLE_DELAY_MS > 0) {
    vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CS_POST_TOGGLE_DELAY_MS));
  }

  if (SD_EXTCS_CS_I2C_SETTLE_US > 0) {
    ets_delay_us(SD_EXTCS_CS_I2C_SETTLE_US);
  }

  return err;
}

static inline esp_err_t sd_extcs_assert_cs(void) { return sd_extcs_set_cs_level(false); }

static inline esp_err_t sd_extcs_deassert_cs(void) { return sd_extcs_set_cs_level(true); }

static void sd_extcs_send_dummy_clocks(size_t byte_count) {
  if (!s_cleanup_handle)
    return;
  if (!sd_extcs_lock())
    return;

  ESP_LOGI(TAG, "Issuing %u dummy clock byte(s) with CS high", (unsigned)byte_count);

  // Ensure CS is deasserted (HIGH) during dummy clocks to comply with SPI mode
  // entry and NCR requirements. Keep the return code to log IO extender health.
  esp_err_t cs_ret = sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

  uint8_t dummy = 0xFF;
  for (size_t i = 0; i < byte_count; ++i) {
    spi_transaction_t t_cleanup = {
        .length = 8,
        .tx_buffer = &dummy,
        .rx_buffer = NULL,
    };
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }
  ESP_LOGI(TAG, "Dummy clocks: %u byte(s) with CS=HIGH (cs_ret=%s)",
           (unsigned)byte_count, esp_err_to_name(cs_ret));
  sd_extcs_unlock();
}

static esp_err_t sd_extcs_configure_cleanup_device(uint32_t freq_khz) {
  uint32_t effective_khz = freq_khz ? freq_khz : SD_EXTCS_INIT_FREQ_KHZ;
  if (s_cleanup_handle) {
    spi_bus_remove_device(s_cleanup_handle);
    s_cleanup_handle = NULL;
  }

  spi_device_interface_config_t dev_cfg = {
      .command_bits = 0,
      .address_bits = 0,
      .dummy_bits = 0,
      .clock_speed_hz = effective_khz * 1000,
      .mode = 0,
      .spics_io_num = -1,
      .queue_size = 1,
      .flags = 0,
  };

  esp_err_t ret = spi_bus_add_device(s_host_id, &dev_cfg, &s_cleanup_handle);
  if (ret == ESP_OK) {
    s_active_freq_khz = effective_khz;
    ESP_LOGI(TAG, "Cleanup SPI handle ready @ %u kHz", effective_khz);
  } else {
    ESP_LOGE(TAG, "Failed to configure cleanup SPI handle @%u kHz: %s",
             effective_khz, esp_err_to_name(ret));
  }
  return ret;
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
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
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

static bool sd_extcs_check_miso_health(bool *cs_low_all_ff,
                                       sd_extcs_miso_diag_t *miso_diag) {
  if (cs_low_all_ff)
    *cs_low_all_ff = false;

  if (miso_diag)
    memset(miso_diag, 0, sizeof(*miso_diag));

  if (!s_cleanup_handle)
    return false;

  uint8_t sample_high[4] = {0};
  uint8_t sample_low[4] = {0};
  bool high_all_ff = true;
  bool low_all_ff = true;

  // CS high sampling (bus should idle to 0xFF)
  sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
  for (size_t i = 0; i < sizeof(sample_high); ++i) {
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
    if (spi_device_polling_transmit(s_cleanup_handle, &t_rx) != ESP_OK)
      break;
    sample_high[i] = t_rx.rx_data[0];
    high_all_ff = high_all_ff && (t_rx.rx_data[0] == 0xFF);
  }

  // CS low sampling without command: if everything is 0xFF here, CS may not be
  // asserted electrically or MISO is floating.
  if (sd_extcs_assert_cs() == ESP_OK) {
    ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US);
    for (size_t i = 0; i < sizeof(sample_low); ++i) {
      spi_transaction_t t_rx = {
          .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
          .length = 8,
          .tx_data = {0xFF},
      };
      if (spi_device_polling_transmit(s_cleanup_handle, &t_rx) != ESP_OK)
        break;
      sample_low[i] = t_rx.rx_data[0];
      low_all_ff = low_all_ff && (t_rx.rx_data[0] == 0xFF);
    }
  }
  sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

  if (miso_diag) {
    memcpy(miso_diag->sample_high, sample_high, sizeof(sample_high));
    memcpy(miso_diag->sample_low, sample_low, sizeof(sample_low));
    miso_diag->high_all_ff = high_all_ff;
    miso_diag->low_all_ff = low_all_ff;
  }

  if (!high_all_ff) {
    ESP_LOGW(TAG,
             "MISO health: noise with CS high (samples=%02X %02X %02X %02X)",
             sample_high[0], sample_high[1], sample_high[2], sample_high[3]);
  }
  if (low_all_ff) {
    ESP_LOGW(TAG,
             "MISO health: CS low but MISO stayed 0xFF (samples=%02X %02X %02X %02X)"
             " -> check CS path / pull-ups",
             sample_low[0], sample_low[1], sample_low[2], sample_low[3]);
  }

  if (cs_low_all_ff)
    *cs_low_all_ff = low_all_ff;

  return true;
}

static esp_err_t sd_extcs_reset_and_cmd0(bool *card_idle, bool *saw_non_ff,
                                         bool *cs_low_stuck_warning,
                                         sd_extcs_miso_diag_t *miso_diag) {
  if (card_idle)
    *card_idle = false;
  if (saw_non_ff)
    *saw_non_ff = false;
  if (cs_low_stuck_warning)
    *cs_low_stuck_warning = false;

  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  ESP_LOGI(TAG,
           "CMD0 pre-sequence: CS high -> %u dummy bytes -> CS low -> CMD0",
           (unsigned)SD_EXTCS_CMD0_PRE_CLKS_BYTES);

  // Force SPI mode: CS high then >=80 clocks
  sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
  vTaskDelay(pdMS_TO_TICKS(2));
  sd_extcs_send_dummy_clocks(SD_EXTCS_CMD0_PRE_CLKS_BYTES);
  vTaskDelay(pdMS_TO_TICKS(2));

  bool cs_low_all_ff = false;
  bool miso_checked = sd_extcs_check_miso_health(&cs_low_all_ff, miso_diag);
  bool cs_low_stuck_warning_local = miso_checked && cs_low_all_ff;
  if (cs_low_stuck_warning)
    *cs_low_stuck_warning = cs_low_stuck_warning_local;
  if (cs_low_stuck_warning_local && !s_warned_cs_low_stuck) {
    s_warned_cs_low_stuck = true;
    ESP_LOGW(TAG,
             "MISO stuck high with CS asserted before CMD0; continuing with retries.");
  }

  bool idle_seen = false;
  bool saw_data = false;
  esp_err_t last_err = ESP_ERR_TIMEOUT;
  int all_ff_attempts = 0;
  enum {
    CMD0_DUMP_LEN = SD_EXTCS_CMD0_RESP_WINDOW_BYTES * 3 + 1,
    CMD0_RESULT_LEN = SD_EXTCS_CMD0_RESULT_STR_LIMIT,
  };
  char last_dump[CMD0_DUMP_LEN];
  last_dump[0] = '\0';
  char last_result[CMD0_RESULT_LEN];
  last_result[0] = '\0';
  uint8_t last_r1 = 0xFF;
  bool last_r1_valid = false;
  int last_idx_valid = -1;
  bool last_bit7_violation = false;
  bool slow_path_applied = false;
  int idle_attempt = -1;

  bool cs_low_stuck_warning_local = miso_checked && cs_low_all_ff;
  if (cs_low_stuck_warning)
    *cs_low_stuck_warning = cs_low_stuck_warning_local;
  bool log_cs_warning = cs_low_stuck_warning_local && !s_warned_cs_low_stuck;

  if (!s_cmd0_diag_logged) {
    s_cmd0_diag_logged = true;
    ESP_LOGI(TAG,
             "CMD0 preamble: dummy_bytes=%u cs_pre_delay_us=%u ioext(latched=%u input=%u)",
             (unsigned)SD_EXTCS_CMD0_PRE_CLKS_BYTES,
             (unsigned)SD_EXTCS_CS_PRE_CMD0_DELAY_US, s_last_cs_latched,
             s_last_cs_input);
  }

  for (int attempt = 0; attempt < SD_EXTCS_CMD0_RETRIES; ++attempt) {
    uint8_t frame[6] = {0x40 | 0, 0, 0, 0, 0, 0x95};
    uint8_t raw[SD_EXTCS_CMD0_RESP_WINDOW_BYTES] = {0};
    size_t raw_len = 0;
    bool valid_r1 = false;
    uint8_t r1 = 0xFF;
    int idx_valid = -1;
    int idle_idx = -1;
    bool bit7_violation = false;
    bool all_ff = true;
    bool saw_non_ff_byte = false;
    size_t ff_before_resp = 0;

    esp_err_t err = sd_extcs_assert_cs();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "CMD0 try %d/%d: CS assert failed: %s", attempt + 1,
               SD_EXTCS_CMD0_RETRIES, esp_err_to_name(err));
      last_err = err;
      sd_extcs_unlock();
      return err;
    }

    if (attempt == 0 && SD_EXTCS_CS_PRE_CMD0_DELAY_US > 0) {
      ets_delay_us(SD_EXTCS_CS_PRE_CMD0_DELAY_US);
    }

    ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US);
    ets_delay_us(SD_EXTCS_CMD0_PRE_CMD_DELAY_US);

    spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
    err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD0 try %d/%d TX failed: %s", attempt + 1,
               SD_EXTCS_CMD0_RETRIES, esp_err_to_name(err));
      last_err = err;
      sd_extcs_deassert_cs();
      ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
      sd_extcs_send_dummy_clocks(1);
      vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CMD0_BACKOFF_MS));
      continue;
    }

    for (size_t i = 0; i < SD_EXTCS_CMD0_RESP_WINDOW_BYTES; ++i) {
      spi_transaction_t t_rx = {
          .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
          .length = 8,
          .tx_data = {0xFF},
      };
      err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      if (err != ESP_OK)
        break;

      uint8_t byte = t_rx.rx_data[0];
      raw[raw_len++] = byte;
      if (!saw_non_ff_byte && byte == 0xFF)
        ff_before_resp++;

      if (byte != 0xFF) {
        saw_data = true;
        saw_non_ff_byte = true;
        all_ff = false;
        if ((byte & 0x80) != 0) {
          bit7_violation = true;
          continue; // Not a valid R1 byte
        }

        if (byte == 0x01 && idle_idx < 0)
          idle_idx = i;

        if (idx_valid < 0 || byte == 0x01) {
          idx_valid = i;
          r1 = byte;
          valid_r1 = true;
        }
      }
    }

    if (idle_idx >= 0) {
      r1 = 0x01;
      idx_valid = idle_idx;
      valid_r1 = true;
    }

    if (all_ff)
      all_ff_attempts++;

    esp_err_t cs_rel_err = sd_extcs_deassert_cs();
    ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
    uint8_t post_rx = 0xFF;
    spi_transaction_t t_post = {.length = 8, .tx_buffer = &post_rx};
    spi_device_polling_transmit(s_cleanup_handle, &t_post);
    ets_delay_us(50);

    char dump[CMD0_DUMP_LEN];
    const size_t rx_preview_len = (raw_len > 16) ? 16 : raw_len;
    size_t dump_len = sd_extcs_safe_hex_dump(raw, rx_preview_len, dump,
                                             sizeof(dump), true);
    const char *dump_ptr = dump_len ? dump : "";
    const size_t dump_width = sd_extcs_strnlen_cap(dump_ptr, SD_EXTCS_CMD0_RAW_STR_LIMIT);

    char cmd_hex[32];
    size_t cmd_hex_len = sd_extcs_safe_hex_dump(frame, sizeof(frame), cmd_hex,
                                                sizeof(cmd_hex), true);
    const char *cmd_hex_ptr = cmd_hex_len ? cmd_hex : "";
    int first_valid_idx = (idx_valid >= 0) ? (idx_valid + 1) : -1;

    char result_str[CMD0_RESULT_LEN];
    if (valid_r1) {
      char r1_bits[64] = {0};
      sd_extcs_decode_r1(r1, r1_bits, sizeof(r1_bits));
      int r1_width = (int)sd_extcs_strnlen_cap(r1_bits, SD_EXTCS_R1_BITS_MAX);
      sd_extcs_safe_snprintf(result_str, sizeof(result_str),
                             "R1=0x%02X (%.*s) ff_before_resp=%u idx=%d bit7=%u",
                             r1, r1_width, r1_bits, (unsigned)ff_before_resp,
                             first_valid_idx, bit7_violation ? 1 : 0);
    } else if (bit7_violation) {
      sd_extcs_safe_snprintf(result_str, sizeof(result_str),
                             "no valid R1 (bit7=1) ff_before_resp=%u idx=%d",
                             (unsigned)ff_before_resp, first_valid_idx);
    } else {
      sd_extcs_safe_snprintf(result_str, sizeof(result_str),
                             "timeout/all-FF ff_before_resp=%u idx=%d",
                             (unsigned)ff_before_resp, first_valid_idx);
    }

    ESP_LOGI(TAG,
             "CMD0 try %d/%d @%u kHz cs_pre=%u us tx[%zu]=%s rx16[%zu]=%.*s idx=%d -> %s (cs_release=%s)",
             attempt + 1, SD_EXTCS_CMD0_RETRIES, s_active_freq_khz,
             SD_EXTCS_CMD0_PRE_CMD_DELAY_US, sizeof(frame), cmd_hex_ptr,
             rx_preview_len, (int)dump_width, dump_ptr, first_valid_idx,
             result_str, esp_err_to_name(cs_rel_err));

    sd_extcs_safe_snprintf(last_result, sizeof(last_result), "%s", result_str);
    sd_extcs_safe_snprintf(last_dump, sizeof(last_dump), "%.*s", (int)dump_width,
                           dump_ptr);
    last_r1 = r1;
    last_r1_valid = valid_r1;
    last_idx_valid = idx_valid;
    last_bit7_violation = bit7_violation;

    if (valid_r1 && r1 == 0x01) {
      idle_seen = true;
      idle_attempt = attempt;
      last_err = ESP_OK;
      break;
    }

    if (valid_r1 && r1 == 0x05) {
      sd_extcs_deassert_cs();
      ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
      sd_extcs_send_dummy_clocks(SD_EXTCS_CMD0_PRE_CLKS_BYTES);
      ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
      vTaskDelay(pdMS_TO_TICKS(1));
      last_err = ESP_ERR_INVALID_RESPONSE;
      continue;
    }

    if (valid_r1) {
      last_err = ESP_ERR_INVALID_RESPONSE;
    } else {
      last_err = saw_data ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
    }
    sd_extcs_deassert_cs();
    ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
    sd_extcs_send_dummy_clocks(SD_EXTCS_CMD0_EXTRA_IDLE_CLKS_BYTES);
    ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

    if (!slow_path_applied && attempt + 1 >= 2) {
      esp_err_t slow_ret = sd_extcs_configure_cleanup_device(SD_EXTCS_CMD0_SLOW_FREQ_KHZ);
      slow_path_applied = (slow_ret == ESP_OK);
      if (slow_path_applied) {
        ESP_LOGW(TAG, "CMD0 anomaly: lowering init clock to %u kHz for retry",
                 SD_EXTCS_CMD0_SLOW_FREQ_KHZ);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CMD0_BACKOFF_MS));
  }

  if (card_idle)
    *card_idle = idle_seen;
  if (saw_non_ff)
    *saw_non_ff = saw_data;

  // Ensure CS returns high and provide a settling window before leaving the
  // routine, regardless of success/failure.
  sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
  sd_extcs_send_dummy_clocks(1);
  vTaskDelay(pdMS_TO_TICKS(1));

  if (log_cs_warning) {
    if (idle_attempt >= 0 && idle_attempt < 2) {
      ESP_LOGI(TAG,
               "MISO stuck high with CS asserted before CMD0 (diagnostic)"
               " cleared after %d attempt(s).",
               idle_attempt + 1);
    } else {
      ESP_LOGW(TAG,
               "MISO stuck high with CS asserted before CMD0; continuing with retries.");
    }
    s_warned_cs_low_stuck = true;
  }

  if (!idle_seen) {
    const char *last_dump_ptr = last_dump[0] ? last_dump : "<none>";
    const int dump_print_len =
        (int)sd_extcs_strnlen_cap(last_dump_ptr, sizeof(last_dump) - 1);
    const char *last_result_ptr = last_result[0] ? last_result : "<none>";
    const int result_print_len =
        (int)sd_extcs_strnlen_cap(last_result_ptr, sizeof(last_result) - 1);
    int first_valid_idx = (last_idx_valid >= 0) ? (last_idx_valid + 1) : -1;
    ESP_LOGW(
        TAG,
        "CMD0 failed after %d tries @%u kHz (last_r1=0x%02X valid=%d saw_non_ff=%d bit7_seen=%d first_valid_idx=%d raw=%.*s -> %.*s)",
        SD_EXTCS_CMD0_RETRIES, s_active_freq_khz, last_r1, last_r1_valid,
        saw_data, last_bit7_violation ? 1 : 0, first_valid_idx, dump_print_len,
        last_dump_ptr, result_print_len, last_result_ptr);
    if (all_ff_attempts >= SD_EXTCS_CMD0_RETRIES) {
      ESP_LOGE(TAG, "MISO stuck high or CS inactive: saw only 0xFF for CMD0 across all retries."
                   " Check SD card power, CS wiring, and pull-ups.");
    }
  }

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

  esp_err_t err = sd_extcs_assert_cs();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS Assert failed before CMD%u: %s", cmd,
             esp_err_to_name(err));
    sd_extcs_unlock();
    return err;
  }

  ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US);

  spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
  err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u TX failed: %s", cmd, esp_err_to_name(err));
    sd_extcs_deassert_cs();
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
      spi_transaction_t t_rx = {
          .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
          .length = 8,
          .tx_data = {0xFF},
      };
      esp_err_t rx_err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      if (rx_err != ESP_OK) {
        err = rx_err;
        break;
      }
      response[i] = t_rx.rx_data[0];
    }
  }

  sd_extcs_deassert_cs();
  sd_extcs_send_dummy_clocks(1);
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

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

  esp_err_t ret = sd_extcs_assert_cs();
  if (ret != ESP_OK) {
    // If CS fails (e.g. I2C timeout), we can't trust the transaction.
    // But we must return an error compatible with storage stack.
    ESP_LOGW(TAG, "CS Assert Failed: %s", esp_err_to_name(ret));
    sd_extcs_unlock();
    return ESP_ERR_TIMEOUT;
  }

  // Explicit setup time for manual CS
  ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US);

  // 2. Delegate to standard SDSPI implementation
  ret = s_original_do_transaction(slot, cmdinfo);

  // 3. Deassert CS
  sd_extcs_deassert_cs(); // Best effort deassert
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

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
  s_warned_cs_low_stuck = false;

  uint32_t init_khz = SD_EXTCS_INIT_FREQ_KHZ;
  if (init_khz > SD_EXTCS_CMD0_SLOW_FREQ_KHZ)
    init_khz = SD_EXTCS_CMD0_SLOW_FREQ_KHZ;

  s_active_freq_khz = init_khz;
  ESP_LOGI(TAG, "Low-speed init at %u kHz (cmd0 slow path=%u kHz)",
           s_active_freq_khz, SD_EXTCS_CMD0_SLOW_FREQ_KHZ);
  sd_extcs_configure_cleanup_device(init_khz);

  bool card_idle = false;
  bool saw_non_ff = false;
  bool cs_low_stuck_warning = false;
  sd_extcs_miso_diag_t precheck_diag = {0};
  esp_err_t err = sd_extcs_reset_and_cmd0(&card_idle, &saw_non_ff,
                                          &cs_low_stuck_warning, &precheck_diag);
  const char *cs_low_stuck_note =
      cs_low_stuck_warning
          ? " (MISO stuck high while CS asserted before CMD0; check CS path)"
          : "";
  if (!card_idle) {
    s_extcs_state = saw_non_ff ? SD_EXTCS_STATE_INIT_FAIL
                               : SD_EXTCS_STATE_ABSENT;
    ESP_LOGW(TAG,
             "CMD0 failed: state=%s err=%s (saw_non_ff=%d miso_precheck_all_ff=%d)%s"
             " cs_level=%d host=%d freq=%u kHz"
             " miso_high=%02X %02X %02X %02X miso_low=%02X %02X %02X %02X",
             sd_extcs_state_str(s_extcs_state), esp_err_to_name(err),
             saw_non_ff, cs_low_stuck_warning, cs_low_stuck_note, s_cs_level,
             s_host_id,
             s_active_freq_khz, precheck_diag.sample_high[0],
             precheck_diag.sample_high[1], precheck_diag.sample_high[2],
             precheck_diag.sample_high[3], precheck_diag.sample_low[0],
             precheck_diag.sample_low[1], precheck_diag.sample_low[2],
             precheck_diag.sample_low[3]);
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

  // CMD58: read OCR with bounded retries for deterministic behavior
  uint8_t resp_r3[5] = {0};
  bool ocr_ok = false;
  int ocr_attempt = 0;
  for (int attempt = 0; attempt < SD_EXTCS_CMD58_RETRIES; ++attempt) {
    ocr_attempt = attempt + 1;
    memset(resp_r3, 0, sizeof(resp_r3));
    err = sd_extcs_send_command(58, 0x00000000, 0xFD, resp_r3, sizeof(resp_r3),
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err == ESP_OK && resp_r3[0] == 0x00) {
      ocr_ok = true;
      break;
    }
    ESP_LOGW(TAG, "CMD58 attempt %d/%d resp=0x%02X err=%s", ocr_attempt,
             SD_EXTCS_CMD58_RETRIES, resp_r3[0], esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(25));
  }

  if (!ocr_ok) {
    ESP_LOGE(TAG, "CMD58 failed after %d attempt(s). Insert SD card or check wiring.",
             ocr_attempt);
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    return err != ESP_OK ? err : ESP_ERR_TIMEOUT;
  }

  uint32_t ocr = ((uint32_t)resp_r3[1] << 24) | ((uint32_t)resp_r3[2] << 16) |
                 ((uint32_t)resp_r3[3] << 8) | resp_r3[4];
  bool high_capacity = (ocr & (1 << 30)) != 0;
  ESP_LOGI(TAG, "OCR=0x%08" PRIX32 " (CCS=%d) (attempt=%d)", ocr,
           high_capacity, ocr_attempt);

  return ESP_OK;
}

// --- Public API ---

esp_err_t sd_extcs_mount_card(const char *mount_point, size_t max_files) {
  if (s_mounted)
    return ESP_OK;

  esp_err_t ret;

  uint32_t init_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;
  if (init_freq_khz > SD_EXTCS_CMD0_SLOW_FREQ_KHZ)
    init_freq_khz = SD_EXTCS_CMD0_SLOW_FREQ_KHZ;
  const uint32_t host_target_khz = SD_EXTCS_TARGET_FREQ_KHZ;

  ESP_LOGI(TAG, "Mounting SD (SDSPI ext-CS) init=%u kHz target=%u kHz",
           init_freq_khz, host_target_khz);

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
    sd_extcs_deassert_cs();
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
    ret = sd_extcs_configure_cleanup_device(init_freq_khz);
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
  host.max_freq_khz = init_freq_khz;

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
    s_active_freq_khz = init_freq_khz;
    // Verify user can read simple stats
    sdmmc_card_print_info(stdout, s_card);

    // Increase clock after OCR stable and card enumerated
    uint32_t card_limit_khz = host_target_khz;
    if (s_card && s_card->max_freq_khz > 0) {
      if (s_card->max_freq_khz > init_freq_khz) {
        card_limit_khz = s_card->max_freq_khz;
      } else {
        ESP_LOGW(TAG,
                 "Card max_freq_khz=%u kHz matches init clock; honoring host target",
                 s_card->max_freq_khz);
      }
    }

    esp_err_t clk_ret = sd_extcs_raise_clock(host_target_khz, card_limit_khz,
                                             init_freq_khz);
    ESP_LOGI(TAG,
             "SD clock summary: init=%u kHz -> active=%u kHz (target=%u kHz, card_max=%u kHz)",
             init_freq_khz, s_active_freq_khz, host_target_khz, card_limit_khz);
    if (clk_ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to raise SD SPI clock after mount: %s",
               esp_err_to_name(clk_ret));
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
