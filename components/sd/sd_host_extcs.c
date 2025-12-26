#include "sd_host_extcs.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_bus_shared.h"
#include "io_extension.h"
#include "sd.h"
#include "sdkconfig.h" // Crucial for CONFIG_ARS defines
#include <inttypes.h>
#include <rom/ets_sys.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static esp_err_t sd_extcs_configure_cleanup_device(uint32_t freq_khz);
static void sd_extcs_free_bus_if_idle(void);

#ifndef SD_EXTCS_CS_I2C_SETTLE_US
#define SD_EXTCS_CS_I2C_SETTLE_US SD_EXTCS_CS_ASSERT_SETTLE_US
#endif

#ifndef SD_EXTCS_CMD58_RETRIES
#define SD_EXTCS_CMD58_RETRIES SD_EXTCS_CMD0_RETRIES
#endif

#ifndef CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ 100
#endif

#ifndef CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ 20000
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

#ifndef CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES
#define CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES 8
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

#ifndef CONFIG_ARS_SD_EXTCS_CS_READBACK
#define CONFIG_ARS_SD_EXTCS_CS_READBACK 0
#endif

#define SD_EXTCS_INIT_FREQ_KHZ CONFIG_ARS_SD_EXTCS_INIT_FREQ_KHZ
#define SD_EXTCS_TARGET_FREQ_KHZ CONFIG_ARS_SD_EXTCS_TARGET_FREQ_KHZ
#define SD_EXTCS_CMD0_PRE_CLKS_BYTES CONFIG_ARS_SD_EXTCS_CMD0_PRE_CLKS_BYTES
#define SD_EXTCS_CS_POST_TOGGLE_DELAY_MS                                       \
  CONFIG_ARS_SD_EXTCS_CS_POST_TOGGLE_DELAY_MS
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
#define SD_EXTCS_CS_ASSERT_WAIT_US 300
#define SD_EXTCS_CS_DEASSERT_WAIT_US 50
#define SD_EXTCS_CS_READBACK_RETRIES 3
#define SD_EXTCS_CS_READBACK_RETRY_US 600
#define SD_EXTCS_POST_DEASSERT_DUMMY_BYTES 2
#define SD_EXTCS_CMD0_MISO_PROBE_BYTES 4
#define SD_EXTCS_R1_WINDOW_BYTES 64
#define SD_EXTCS_R1_TRACE_BYTES 16
#define SD_EXTCS_R1_TIMEOUT_US                                            \
  ((uint32_t)((SD_EXTCS_CMD_TIMEOUT_TICKS * 1000000ULL) / configTICK_RATE_HZ))
#define SD_EXTCS_INIT_TIMEOUT_US 1500000
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
static int s_cs_level =
    -1; // -1 = unknown, 0 = asserted (LOW), 1 = deasserted (HIGH)
static i2c_master_dev_handle_t s_ioext_handle = NULL;
static SemaphoreHandle_t s_sd_mutex = NULL;
static sd_extcs_state_t s_extcs_state = SD_EXTCS_STATE_UNINITIALIZED;
static bool s_warned_cs_low_stuck = false;
static bool s_cmd0_diag_logged = false;
static uint8_t s_last_cs_latched = 0xFF;
static uint8_t s_last_cs_input = 0xFF;
static uint8_t s_miso_low_ff_streak = 0;
static uint8_t s_miso_high_noise_streak = 0;
static int64_t s_miso_last_diag_us = 0;
static sd_extcs_sequence_stats_t s_seq_stats = {0};
static bool s_ioext_cs_locked = false;
static bool s_cs_i2c_locked = false;
static bool s_bus_initialized = false;

#define SD_EXTCS_IOEXT_CS_MASK (1 << IO_EXTENSION_IO_4)

typedef struct {
  uint8_t sample_high[4];
  uint8_t sample_low[4];
  bool high_all_ff;
  bool low_all_ff;
} sd_extcs_miso_diag_t;

typedef struct {
  uint8_t raw[SD_EXTCS_R1_TRACE_BYTES];
  size_t raw_len;
  int r1_index; // 1-based index of accepted R1, -1 if none
  bool invalid_seen;
} sd_extcs_r1_trace_t;

// Pointer to original implementation
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks,
                                       sd_extcs_r1_trace_t *trace,
                                       bool init_filter, bool allow_illegal_05);
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
static size_t sd_extcs_safe_hex_dump(const uint8_t *src, size_t src_len,
                                     char *dst, size_t dst_size,
                                     bool leading_space);
static size_t sd_extcs_strnlen_cap(const char *s, size_t max_len);
static size_t sd_extcs_probe_miso_under_cs(uint8_t *out, size_t max_len);
// GCC 12+ aggressively warns about potential truncation; use a bounded helper
// to force null-termination and placate -Wformat-truncation without weakening
// warnings globally.
static size_t sd_extcs_safe_snprintf(char *dst, size_t dst_size,
                                     const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
// Avoids -Waddress false positives when using stack buffers that are never
// NULL.
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
  return xSemaphoreTakeRecursive(s_sd_mutex, SD_EXTCS_CS_LOCK_TIMEOUT) ==
         pdTRUE;
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

static esp_err_t sd_extcs_lock_ioext_bus(void) {
  if (!s_ioext_ready || s_ioext_handle == NULL) {
    ESP_LOGE(TAG, "CS lock failed: IO expander not ready");
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_ioext_cs_locked) {
    if (!io_extension_lock(SD_EXTCS_CS_LOCK_TIMEOUT)) {
      ESP_LOGW(TAG, "CS IOEXT mutex busy");
      return ESP_ERR_TIMEOUT;
    }
    s_ioext_cs_locked = true;
  }

  if (!s_cs_i2c_locked) {
    if (!i2c_bus_shared_lock(SD_EXTCS_CS_LOCK_TIMEOUT)) {
      ESP_LOGW(TAG, "CS I2C lock timeout");
      io_extension_unlock();
      s_ioext_cs_locked = false;
      return ESP_ERR_TIMEOUT;
    }
    s_cs_i2c_locked = true;
  }

  return ESP_OK;
}

static void sd_extcs_unlock_ioext_bus(void) {
  if (s_cs_i2c_locked) {
    i2c_bus_shared_unlock();
    s_cs_i2c_locked = false;
  }
  if (s_ioext_cs_locked) {
    io_extension_unlock();
    s_ioext_cs_locked = false;
  }
}

static esp_err_t sd_extcs_readback_cs_locked(bool assert_low, bool *matched) {
#if CONFIG_ARS_SD_EXTCS_CS_READBACK
  if (matched)
    *matched = false;
  uint8_t latched = 0xFF;
  esp_err_t rb =
      IO_EXTENSION_Read_Output_Latch(IO_EXTENSION_IO_4, &latched);
  if (rb == ESP_OK && matched) {
    *matched = (((latched & SD_EXTCS_IOEXT_CS_MASK) != 0) == (!assert_low));
  }
  return rb;
#else
  if (matched)
    *matched = true;
  return ESP_OK;
#endif
}

static esp_err_t sd_extcs_apply_cs_level_locked(bool assert_low,
                                                const char *ctx) {
  const uint8_t shadow_before = io_extension_get_output_shadow();
  esp_err_t err = ESP_FAIL;
  bool latched_ok = false;

  for (int attempt = 0; attempt < SD_EXTCS_CS_READBACK_RETRIES; ++attempt) {
    err = assert_low ? io_extension_clear_bits_locked(SD_EXTCS_IOEXT_CS_MASK)
                     : io_extension_set_bits_locked(SD_EXTCS_IOEXT_CS_MASK);

    const uint8_t shadow_after = io_extension_get_output_shadow();
    esp_err_t rb = sd_extcs_readback_cs_locked(assert_low, &latched_ok);
    const bool shadow_level = (shadow_after & SD_EXTCS_IOEXT_CS_MASK) != 0;
    const bool want_level = !assert_low;

    if (err == ESP_OK && (!CONFIG_ARS_SD_EXTCS_CS_READBACK || latched_ok)) {
      s_cs_level = shadow_level ? 1 : 0;
      s_last_cs_latched = latched_ok ? (want_level ? 1 : 0) : s_cs_level;
      s_last_cs_input = s_last_cs_latched;
      ESP_LOGD(TAG,
               "%s OK (attempt=%d shadow %02X->%02X latched=%d streak=%" PRIu32
               ")",
               ctx, attempt + 1, shadow_before, shadow_after,
               latched_ok ? (want_level ? 1 : 0) : -1,
               i2c_bus_shared_get_error_streak());
      return ESP_OK;
    }

    ESP_LOGW(TAG,
             "%s attempt %d: err=%s rb=%s shadow %02X->%02X latched_ok=%d "
             "i2c_streak=%" PRIu32,
             ctx, attempt + 1, esp_err_to_name(err), esp_err_to_name(rb),
             shadow_before, shadow_after, latched_ok ? 1 : 0,
             i2c_bus_shared_get_error_streak());

    if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_TIMEOUT ||
        err == ESP_FAIL) {
      esp_err_t rec = i2c_bus_shared_recover_locked();
      if (rec != ESP_OK) {
        ESP_LOGW(TAG, "%s: I2C recover failed: %s", ctx,
                 esp_err_to_name(rec));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  return err;
}

static void sd_extcs_seq_reset(uint32_t init_khz, uint32_t target_khz) {
  s_seq_stats.pre_clks_bytes = SD_EXTCS_CMD0_PRE_CLKS_BYTES;
  s_seq_stats.init_freq_khz = init_khz;
  s_seq_stats.target_freq_khz = target_khz;
  s_seq_stats.cmd0_seen = false;
  s_seq_stats.cmd8_seen = false;
  s_seq_stats.acmd41_seen = false;
  s_seq_stats.cmd58_seen = false;
  s_seq_stats.final_state = s_extcs_state;
}

static inline void sd_extcs_seq_mark_state(sd_extcs_state_t state) {
  s_seq_stats.final_state = state;
}

esp_err_t sd_extcs_get_sequence_stats(sd_extcs_sequence_stats_t *out_stats) {
  if (!out_stats) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_stats = s_seq_stats;
  return ESP_OK;
}

static size_t sd_extcs_safe_snprintf(char *dst, size_t dst_size,
                                     const char *fmt, ...) {
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

static size_t sd_extcs_safe_hex_dump(const uint8_t *src, size_t src_len,
                                     char *dst, size_t dst_size,
                                     bool leading_space) {
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

  return sd_extcs_safe_snprintf(
      buf, len, "%s%s%s%s%s%s%s", (r1 & 0x01) ? "IDLE" : "READY",
      (r1 & 0x02) ? " ERASE_RESET" : "", (r1 & 0x04) ? " ILLEGAL_CMD" : "",
      (r1 & 0x08) ? " CRC_ERR" : "", (r1 & 0x10) ? " ERASE_SEQ" : "",
      (r1 & 0x20) ? " ADDR_ERR" : "", (r1 & 0x40) ? " PARAM_ERR" : "");
}

static size_t sd_extcs_probe_miso_under_cs(uint8_t *out, size_t max_len) {
  if (!out || max_len == 0 || !s_cleanup_handle)
    return 0;
  size_t sampled = 0;
  for (size_t i = 0; i < max_len; ++i) {
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
    if (spi_device_polling_transmit(s_cleanup_handle, &t_rx) != ESP_OK)
      break;
    out[i] = t_rx.rx_data[0];
    sampled++;
  }
  return sampled;
}

static esp_err_t sd_extcs_raise_clock(uint32_t host_target_khz,
                                      uint32_t card_limit_khz,
                                      uint32_t init_khz) {
  const uint32_t candidates[] = {host_target_khz,
                                 CONFIG_ARS_SD_EXTCS_RECOVERY_FREQ_KHZ,
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
      ESP_LOGI(
          TAG,
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
static esp_err_t sd_extcs_assert_cs(void) {
  esp_err_t lock_ret = sd_extcs_lock_ioext_bus();
  if (lock_ret != ESP_OK) {
    return lock_ret;
  }

  esp_err_t err = sd_extcs_apply_cs_level_locked(true, "CS LOW");

  if (err != ESP_OK) {
    sd_extcs_unlock_ioext_bus();
    return err;
  }

  if (SD_EXTCS_CS_I2C_SETTLE_US > 0)
    ets_delay_us(SD_EXTCS_CS_I2C_SETTLE_US);
  ets_delay_us(SD_EXTCS_CS_ASSERT_WAIT_US);
  return ESP_OK;
}

static inline esp_err_t sd_extcs_deassert_cs(void) {
  esp_err_t lock_ret = sd_extcs_lock_ioext_bus();
  if (lock_ret != ESP_OK) {
    return lock_ret;
  }

  esp_err_t err = sd_extcs_apply_cs_level_locked(false, "CS HIGH");

  if (SD_EXTCS_CS_POST_TOGGLE_DELAY_MS > 0) {
    vTaskDelay(pdMS_TO_TICKS(SD_EXTCS_CS_POST_TOGGLE_DELAY_MS));
  }
  if (SD_EXTCS_CS_I2C_SETTLE_US > 0)
    ets_delay_us(SD_EXTCS_CS_I2C_SETTLE_US);
  if (s_cleanup_handle) {
    uint8_t dummy = 0xFF;
    for (size_t i = 0; i < SD_EXTCS_POST_DEASSERT_DUMMY_BYTES; ++i) {
      spi_transaction_t t_cleanup = {
          .length = 8, .tx_buffer = &dummy, .rx_buffer = NULL};
      spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
    }
  }
  ets_delay_us(SD_EXTCS_CS_DEASSERT_WAIT_US);

  sd_extcs_unlock_ioext_bus();

  return err;
}

static void sd_extcs_send_dummy_clocks(size_t byte_count) {
  if (!s_cleanup_handle)
    return;
  if (!sd_extcs_lock())
    return;

  ESP_LOGI(TAG, "Issuing %u dummy clock byte(s) with CS high",
           (unsigned)byte_count);

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
  ESP_LOGD(TAG, "Dummy clocks: %u byte(s) with CS=HIGH (cs_ret=%s)",
           (unsigned)byte_count, esp_err_to_name(cs_ret));
  sd_extcs_unlock();
}

static esp_err_t sd_extcs_configure_cleanup_device(uint32_t freq_khz) {
  if (!s_bus_initialized) {
    ESP_LOGE(TAG, "Cleanup device add failed: SPI bus not initialized");
    return ESP_ERR_INVALID_STATE;
  }
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

static void sd_extcs_free_bus_if_idle(void) {
  if (s_cleanup_handle) {
    spi_bus_remove_device(s_cleanup_handle);
    s_cleanup_handle = NULL;
  }
  if (s_bus_initialized) {
    spi_bus_free(s_host_id);
    s_bus_initialized = false;
  }
}

static esp_err_t sd_extcs_probe_cs_line(void) {
  // Toggle CS via the IO extender to ensure the IO path is alive.
  // This catches the "CS stuck high" hardware fault that makes CMD0 always
  // return 0xFF.
  //
  // NOTE: The CH32V003 does not reliably support readback of output registers
  // via I2C. We trust the shadow state maintained by the IO extension driver
  // after successful writes. If the I2C write succeeds, we consider the toggle
  // successful.
  if (!sd_extcs_lock())
    return ESP_ERR_TIMEOUT;

  esp_err_t lock_ret = sd_extcs_lock_ioext_bus();
  if (lock_ret != ESP_OK) {
    sd_extcs_unlock();
    return lock_ret;
  }

  esp_err_t err = sd_extcs_apply_cs_level_locked(false, "CS probe HIGH");
  uint8_t shadow = io_extension_get_output_shadow();
  int cs_high = (shadow & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
  if (err != ESP_OK) {
    sd_extcs_unlock_ioext_bus();
    sd_extcs_unlock();
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(2));

  err = sd_extcs_apply_cs_level_locked(true, "CS probe LOW");
  shadow = io_extension_get_output_shadow();
  int cs_low = (shadow & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
  if (err != ESP_OK) {
    sd_extcs_unlock_ioext_bus();
    sd_extcs_unlock();
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(2));

  err = sd_extcs_apply_cs_level_locked(false, "CS probe RESTORE");
  shadow = io_extension_get_output_shadow();
  int cs_restore = (shadow & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
  sd_extcs_unlock_ioext_bus();
  sd_extcs_unlock();

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CS probe: failed to restore high: %s", esp_err_to_name(err));
    return err;
  }

  // All I2C writes succeeded - the toggle is considered successful
  // Note: We cannot verify via readback because CH32V003 doesn't reliably
  // support it
  ESP_LOGI(TAG,
           "CS probe: IOEXT4 toggle OK (I2C writes successful shadow=%d/%d/%d)",
           cs_high, cs_low, cs_restore);
  return ESP_OK;
}

static esp_err_t sd_extcs_diag_toggle_and_cmd0(void) {
  if (!s_ioext_ready || s_ioext_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = ESP_OK;
  int toggle_fail = 0;
  uint8_t shadow_before = io_extension_get_output_shadow();
  uint8_t shadow_after = shadow_before;

  if (!sd_extcs_lock()) {
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t lock_ret = sd_extcs_lock_ioext_bus();
  if (lock_ret != ESP_OK) {
    sd_extcs_unlock();
    return lock_ret;
  }

  for (int i = 0; i < 10; ++i) {
    esp_err_t clr_ret =
        sd_extcs_apply_cs_level_locked(true, "Diag-CS LOW toggle");
    if (clr_ret != ESP_OK) {
      toggle_fail++;
      ret = clr_ret;
    }
    ets_delay_us(80);
    esp_err_t set_ret =
        sd_extcs_apply_cs_level_locked(false, "Diag-CS HIGH toggle");
    if (set_ret != ESP_OK) {
      toggle_fail++;
      ret = set_ret;
    }
    ets_delay_us(80);
  }
  shadow_after = io_extension_get_output_shadow();

  sd_extcs_unlock_ioext_bus();
  sd_extcs_unlock();

  ESP_LOGI(TAG,
           "Diag-CS: toggled 10x fail=%d shadow_before=0x%02X shadow_after=0x%02X "
           "i2c_streak=%" PRIu32,
           toggle_fail, shadow_before, shadow_after,
           i2c_bus_shared_get_error_streak());

  if (!s_cleanup_handle) {
    return (ret == ESP_OK) ? ESP_ERR_INVALID_STATE : ret;
  }

  uint8_t rx[16] = {0xFF};
  uint8_t first_r1 = 0xFF;
  int first_idx = -1;
  esp_err_t cmd_ret = ESP_OK;

  if (!sd_extcs_lock()) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t cs_high_ret = sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);

  uint8_t dummy = 0xFF;
  for (size_t i = 0; i < SD_EXTCS_CMD0_PRE_CLKS_BYTES; ++i) {
    spi_transaction_t t_dummy = {.length = 8, .tx_buffer = &dummy};
    esp_err_t tx_ret = spi_device_polling_transmit(s_cleanup_handle, &t_dummy);
    if (tx_ret != ESP_OK && cmd_ret == ESP_OK) {
      cmd_ret = tx_ret;
    }
  }

  esp_err_t cs_low_ret = sd_extcs_assert_cs();
  ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US + SD_EXTCS_CS_ASSERT_WAIT_US);

  uint8_t frame[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
  esp_err_t cmd_tx_ret = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
  if (cmd_tx_ret != ESP_OK && cmd_ret == ESP_OK) {
    cmd_ret = cmd_tx_ret;
  }

  for (size_t i = 0; i < sizeof(rx); ++i) {
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
    if (spi_device_polling_transmit(s_cleanup_handle, &t_rx) != ESP_OK) {
      if (cmd_ret == ESP_OK)
        cmd_ret = ESP_ERR_TIMEOUT;
      break;
    }
    rx[i] = t_rx.rx_data[0];
    if (first_idx < 0 && rx[i] != 0xFF) {
      first_idx = (int)(i + 1);
      first_r1 = rx[i];
    }
  }

  sd_extcs_deassert_cs();
  spi_transaction_t t_post = {.length = 8, .tx_buffer = &dummy};
  spi_device_polling_transmit(s_cleanup_handle, &t_post);

  sd_extcs_unlock();

  char dump[64];
  size_t dump_len =
      sd_extcs_safe_hex_dump(rx, sizeof(rx), dump, sizeof(dump), true);
  const char *dump_ptr = dump_len ? dump : "<none>";

  ESP_LOGI(TAG,
           "Diag-CMD0 once: cs_high=%s cs_low=%s r1=0x%02X idx=%d rx=%s "
           "shadow=0x%02X i2c_streak=%" PRIu32,
           esp_err_to_name(cs_high_ret), esp_err_to_name(cs_low_ret), first_r1,
           first_idx, dump_ptr, io_extension_get_output_shadow(),
           i2c_bus_shared_get_error_streak());

  if (cmd_ret == ESP_OK && (cs_high_ret != ESP_OK || cs_low_ret != ESP_OK)) {
    cmd_ret = (cs_high_ret != ESP_OK) ? cs_high_ret : cs_low_ret;
  }
  return (cmd_ret == ESP_OK) ? ret : cmd_ret;
}

static esp_err_t sd_extcs_wait_for_r1(uint8_t *out, uint32_t timeout_us,
                                      size_t max_bytes,
                                      sd_extcs_r1_trace_t *trace,
                                      bool init_filter,
                                      bool allow_illegal_05) {
  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;

  if (trace) {
    memset(trace, 0, sizeof(*trace));
    trace->r1_index = -1;
  }

  int64_t deadline = esp_timer_get_time() + timeout_us;
  uint8_t rx = 0xFF;
  uint8_t last_seen = 0xFF;
  bool invalid_seen = false;
  size_t sampled = 0;

  while (sampled < max_bytes && esp_timer_get_time() < deadline) {
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
    esp_err_t err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
    if (err != ESP_OK)
      return err;

    rx = t_rx.rx_data[0];
    last_seen = rx;
    if (trace && trace->raw_len < sizeof(trace->raw)) {
      trace->raw[trace->raw_len++] = rx;
    }
    sampled++;

    if (rx == 0xFF)
      continue;
    if (rx & 0x80) {
      invalid_seen = true;
      continue;
    }

    if (init_filter) {
      const bool is_idle = (rx == 0x01);
      const bool is_ready = (rx == 0x00);
      const bool is_illegal_cmd = (rx == 0x05);
      const uint8_t error_bits = rx & 0x7E;
      const int error_count = __builtin_popcount((unsigned)error_bits);
      const bool fatal_noise =
          (rx == 0x5F) || (error_count >= 3) || ((error_bits & 0x60) != 0);

      if (fatal_noise) {
        invalid_seen = true;
        continue;
      }

      if (!is_idle && !is_ready &&
          !(allow_illegal_05 && is_illegal_cmd)) {
        if (error_bits != 0) {
          invalid_seen = true;
        }
        continue;
      }
    }

    if (trace)
      trace->invalid_seen = invalid_seen;
    if (trace)
      trace->r1_index = (int)sampled;
    if (out)
      *out = rx;
    return ESP_OK;
  }

  if (trace)
    trace->invalid_seen = invalid_seen;
  if (out)
    *out = last_seen;

  if (invalid_seen)
    return ESP_ERR_INVALID_RESPONSE;
  return ESP_ERR_TIMEOUT;
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
    // Allow additional settle time for IO extender propagation and bus release
    // when toggling CS via I2C.
    ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US + 80);
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

  bool cs_low_ff_detected = low_all_ff && (s_cs_level == 0);
  bool high_noise_detected = !high_all_ff;

  s_miso_low_ff_streak =
      cs_low_ff_detected ? (uint8_t)(s_miso_low_ff_streak + 1) : 0;
  s_miso_high_noise_streak =
      high_noise_detected ? (uint8_t)(s_miso_high_noise_streak + 1) : 0;

  int64_t now_us = esp_timer_get_time();
  bool log_allowed =
      (s_miso_last_diag_us == 0) || ((now_us - s_miso_last_diag_us) > 2000000);

  if (high_noise_detected && s_miso_high_noise_streak >= 2 && log_allowed) {
    s_miso_last_diag_us = now_us;
    ESP_LOGW(TAG,
             "MISO health: noise with CS high (samples=%02X %02X %02X %02X)",
             sample_high[0], sample_high[1], sample_high[2], sample_high[3]);
  }
  if (cs_low_ff_detected && s_miso_low_ff_streak >= 3 && log_allowed) {
    s_miso_last_diag_us = now_us;
    ESP_LOGW(TAG,
             "MISO health: CS asserted but MISO stayed 0xFF (samples=%02X %02X "
             "%02X %02X);"
             " recheck CS path or pulls",
             sample_low[0], sample_low[1], sample_low[2], sample_low[3]);
  } else if (cs_low_ff_detected && log_allowed) {
    s_miso_last_diag_us = now_us;
    ESP_LOGD(TAG,
             "MISO health (diag only): CS asserted and MISO idle=0xFF "
             "(samples=%02X %02X %02X %02X)",
             sample_low[0], sample_low[1], sample_low[2], sample_low[3]);
  }

  if (cs_low_all_ff)
    *cs_low_all_ff = cs_low_ff_detected && s_miso_low_ff_streak >= 2;

  return true;
}

#if CONFIG_ARS_SD_EXTCS_MISO_STUCK_DEBUG
static void sd_extcs_force_cs_low_miso_debug(void) {
  if (!s_cleanup_handle) {
    ESP_LOGW(TAG, "MISO debug skipped: cleanup handle missing");
    return;
  }
  if (!sd_extcs_lock()) {
    ESP_LOGW(TAG, "MISO debug skipped: CS lock busy");
    return;
  }

  esp_err_t err = sd_extcs_assert_cs();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "MISO debug: CS assert failed: %s", esp_err_to_name(err));
    sd_extcs_unlock();
    return;
  }

  ets_delay_us(SD_EXTCS_CS_ASSERT_SETTLE_US + 80);

  uint8_t rx[CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES] = {0};
  bool all_same = true;
  uint8_t first = 0xFF;
  size_t sampled = 0;

  for (size_t i = 0; i < CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES; ++i) {
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {0xFF},
    };
    err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
    if (err != ESP_OK)
      break;
    rx[i] = t_rx.rx_data[0];
    if (i == 0) {
      first = rx[i];
    } else if (rx[i] != first) {
      all_same = false;
    }
    sampled++;
  }

  int miso_gpio = gpio_get_level(CONFIG_ARS_SD_MISO);
  sd_extcs_deassert_cs();
  ets_delay_us(SD_EXTCS_CS_DEASSERT_SETTLE_US);
  sd_extcs_send_dummy_clocks(1);
  sd_extcs_unlock();

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "MISO debug aborted after %u sample(s): %s", (unsigned)sampled,
             esp_err_to_name(err));
    return;
  }

  char dump[CONFIG_ARS_SD_EXTCS_MISO_STUCK_SAMPLE_BYTES * 3 + 1];
  size_t dump_len = sd_extcs_safe_hex_dump(rx, sampled, dump, sizeof(dump), true);
  const char *dump_ptr = dump_len ? dump : "<none>";
  ESP_LOGI(TAG,
           "MISO debug (CS forced LOW): gpio=%d sampled=%u rx=%.*s %s",
           miso_gpio, (unsigned)sampled, (int)dump_len, dump_ptr,
           all_same ? "[CONST]" : "[VAR]");
}
#else
static inline void sd_extcs_force_cs_low_miso_debug(void) {}
#endif

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
  if (miso_diag) {
    uint8_t shadow = io_extension_get_output_shadow();
    int cs_shadow = (shadow & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
    ESP_LOGI(TAG,
             "CMD0 diag pre: shadow=0x%02X cs_shadow=%d MISO idle=%02X %02X "
             "%02X %02X cs_low=%02X %02X %02X %02X high_ff=%d low_ff=%d",
             shadow, cs_shadow, miso_diag->sample_high[0],
             miso_diag->sample_high[1], miso_diag->sample_high[2],
             miso_diag->sample_high[3], miso_diag->sample_low[0],
             miso_diag->sample_low[1], miso_diag->sample_low[2],
             miso_diag->sample_low[3], miso_diag->high_all_ff ? 1 : 0,
             miso_diag->low_all_ff ? 1 : 0);
  }
  bool cs_low_stuck_warning_local = miso_checked && cs_low_all_ff;
  bool log_cs_warning = cs_low_stuck_warning_local && !s_warned_cs_low_stuck;
  if (cs_low_stuck_warning)
    *cs_low_stuck_warning = cs_low_stuck_warning_local;
  if (log_cs_warning) {
    s_warned_cs_low_stuck = true;
    ESP_LOGW(TAG, "MISO stuck high with CS asserted before CMD0; continuing "
                  "with retries.");
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

  if (!s_cmd0_diag_logged) {
    s_cmd0_diag_logged = true;
    ESP_LOGD(TAG,
             "CMD0 preamble: dummy_bytes=%u cs_pre_delay_us=%u "
             "ioext(latched=%u input=%u)",
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

    uint8_t miso_probe[SD_EXTCS_CMD0_MISO_PROBE_BYTES] = {0};
    size_t miso_probe_len = sd_extcs_probe_miso_under_cs(
        miso_probe, SD_EXTCS_CMD0_MISO_PROBE_BYTES);

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
    size_t dump_len =
        sd_extcs_safe_hex_dump(raw, rx_preview_len, dump, sizeof(dump), true);
    const char *dump_ptr = dump_len ? dump : "";
    const size_t dump_width =
        sd_extcs_strnlen_cap(dump_ptr, SD_EXTCS_CMD0_RAW_STR_LIMIT);

    char cmd_hex[32];
    size_t cmd_hex_len = sd_extcs_safe_hex_dump(frame, sizeof(frame), cmd_hex,
                                                sizeof(cmd_hex), true);
    const char *cmd_hex_ptr = cmd_hex_len ? cmd_hex : "";
    int first_valid_idx = (idx_valid >= 0) ? (idx_valid + 1) : -1;

    uint8_t cs_shadow_now = io_extension_get_output_shadow();
    int cs_shadow_level = (cs_shadow_now & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
    char miso_probe_hex[SD_EXTCS_CMD0_MISO_PROBE_BYTES * 3 + 1];
    size_t miso_probe_hex_len = sd_extcs_safe_hex_dump(
        miso_probe, miso_probe_len, miso_probe_hex, sizeof(miso_probe_hex),
        true);
    const char *miso_probe_ptr =
        miso_probe_hex_len ? miso_probe_hex : "<none>";

    char result_str[CMD0_RESULT_LEN];
    if (valid_r1) {
      char r1_bits[64] = {0};
      sd_extcs_decode_r1(r1, r1_bits, sizeof(r1_bits));
      int r1_width = (int)sd_extcs_strnlen_cap(r1_bits, SD_EXTCS_R1_BITS_MAX);
      sd_extcs_safe_snprintf(
          result_str, sizeof(result_str),
          "R1=0x%02X (%.*s) ff_before_resp=%u idx=%d bit7=%u", r1, r1_width,
          r1_bits, (unsigned)ff_before_resp, first_valid_idx,
          bit7_violation ? 1 : 0);
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
             "CMD0 try %d/%d @%u kHz cs_pre=%u us tx[%zu]=%s rx16[%zu]=%.*s "
             "idx=%d cs_shadow=%d miso_probe=%s -> %s (cs_release=%s)",
             attempt + 1, SD_EXTCS_CMD0_RETRIES, s_active_freq_khz,
             SD_EXTCS_CMD0_PRE_CMD_DELAY_US, sizeof(frame), cmd_hex_ptr,
             rx_preview_len, (int)dump_width, dump_ptr, first_valid_idx,
             cs_shadow_level, miso_probe_ptr, result_str,
             esp_err_to_name(cs_rel_err));

    sd_extcs_safe_snprintf(last_result, sizeof(last_result), "%s", result_str);
    sd_extcs_safe_snprintf(last_dump, sizeof(last_dump), "%.*s",
                           (int)dump_width, dump_ptr);
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
      esp_err_t slow_ret =
          sd_extcs_configure_cleanup_device(SD_EXTCS_CMD0_SLOW_FREQ_KHZ);
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
      ESP_LOGW(TAG, "MISO stuck high with CS asserted before CMD0; continuing "
                    "with retries.");
    }
    s_warned_cs_low_stuck = true;
  }

  if (!idle_seen) {
    const char *classification =
        saw_data ? "WIRED_BUT_NO_RESP" : "ABSENT (all 0xFF)";
    const char *last_dump_ptr = last_dump[0] ? last_dump : "<none>";
    const int dump_print_len =
        (int)sd_extcs_strnlen_cap(last_dump_ptr, sizeof(last_dump) - 1);
    const char *last_result_ptr = last_result[0] ? last_result : "<none>";
    const int result_print_len =
        (int)sd_extcs_strnlen_cap(last_result_ptr, sizeof(last_result) - 1);
    int first_valid_idx = (last_idx_valid >= 0) ? (last_idx_valid + 1) : -1;
    ESP_LOGW(TAG,
             "CMD0 failed after %d tries @%u kHz class=%s (last_r1=0x%02X "
             "valid=%d saw_non_ff=%d bit7_seen=%d first_valid_idx=%d raw=%.*s "
             "-> %.*s)",
             SD_EXTCS_CMD0_RETRIES, s_active_freq_khz, classification, last_r1,
             last_r1_valid, saw_data, last_bit7_violation ? 1 : 0,
             first_valid_idx, dump_print_len, last_dump_ptr, result_print_len,
             last_result_ptr);
    if (all_ff_attempts >= SD_EXTCS_CMD0_RETRIES) {
      ESP_LOGE(TAG, "MISO stuck high or CS inactive: saw only 0xFF for CMD0 "
                    "across all retries."
                    " Check SD card power, CS wiring, and pull-ups.");
    }
  }

  uint8_t shadow_post = io_extension_get_output_shadow();
  int cs_shadow_post = (shadow_post & SD_EXTCS_IOEXT_CS_MASK) ? 1 : 0;
  if (miso_diag) {
    ESP_LOGI(TAG,
             "CMD0 diag post: shadow=0x%02X cs_shadow=%d MISO idle=%02X %02X "
             "%02X %02X cs_low=%02X %02X %02X %02X",
             shadow_post, cs_shadow_post, miso_diag->sample_high[0],
             miso_diag->sample_high[1], miso_diag->sample_high[2],
             miso_diag->sample_high[3], miso_diag->sample_low[0],
             miso_diag->sample_low[1], miso_diag->sample_low[2],
             miso_diag->sample_low[3]);
  } else {
    ESP_LOGI(TAG, "CMD0 diag post: shadow=0x%02X cs_shadow=%d (shadow-only)",
             shadow_post, cs_shadow_post);
  }

  sd_extcs_unlock();
  return last_err;
}

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks,
                                       sd_extcs_r1_trace_t *trace,
                                       bool init_filter,
                                       bool allow_illegal_05) {
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

  spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
  err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u TX failed: %s", cmd, esp_err_to_name(err));
    sd_extcs_deassert_cs();
    sd_extcs_unlock();
    return err;
  }

  sd_extcs_r1_trace_t local_trace = {0};
  sd_extcs_r1_trace_t *active_trace = trace ? trace : &local_trace;

  uint8_t first_byte = 0xFF;
  uint32_t timeout_us =
      (uint32_t)((timeout_ticks * 1000000ULL) / configTICK_RATE_HZ);
  if (timeout_us == 0)
    timeout_us = SD_EXTCS_R1_TIMEOUT_US;

  err = sd_extcs_wait_for_r1(&first_byte, timeout_us, SD_EXTCS_R1_WINDOW_BYTES,
                             active_trace, init_filter, allow_illegal_05);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "CMD%u wait_r1 err=%s r1=0x%02X", cmd, esp_err_to_name(err),
             first_byte);

  ESP_LOGD(TAG, "CMD%u resp0=0x%02X (len=%zu)", cmd, first_byte, response_len);

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

  if (cmd == 55 || cmd == 41) {
    char trace_hex[SD_EXTCS_R1_TRACE_BYTES * 3 + 1];
    size_t dump_len = sd_extcs_safe_hex_dump(active_trace->raw,
                                             active_trace->raw_len, trace_hex,
                                             sizeof(trace_hex), true);
    const char *dump_ptr = dump_len ? trace_hex : "<none>";
    ESP_LOGI(TAG,
             "CMD%u diag: r1=0x%02X idx=%d bytes=%zu/%d invalid=%d rx%s",
             cmd, first_byte, active_trace->r1_index, active_trace->raw_len,
             SD_EXTCS_R1_WINDOW_BYTES, active_trace->invalid_seen ? 1 : 0,
             dump_ptr);
  }

  ESP_LOGI(TAG,
           "CMD%u arg=0x%08" PRIX32 " r1=0x%02X len=%zu freq=%u kHz ret=%s",
           cmd, arg, first_byte, response_len, s_active_freq_khz,
           esp_err_to_name(err));

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

  // 2. Delegate to standard SDSPI implementation
  ret = s_original_do_transaction(slot, cmdinfo);

  // 3. Deassert CS
  sd_extcs_deassert_cs(); // Best effort deassert

#if CONFIG_ARS_SD_EXTCS_TIMING_LOG
  const uint8_t opcode = cmdinfo ? cmdinfo->opcode : 0xFF;
  const int64_t duration_us = esp_timer_get_time() - t_start_us;
  ESP_LOGI(TAG, "CMD%u done in %" PRId64 " us (len=%d, freq=%u kHz, ret=%s)",
           opcode, duration_us, cmdinfo ? cmdinfo->datalen : 0,
           s_active_freq_khz, esp_err_to_name(ret));
#endif

  sd_extcs_unlock();

  return ret;
}

static esp_err_t sd_extcs_low_speed_init(void) {
  gpio_set_pull_mode(CONFIG_ARS_SD_MISO, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(CONFIG_ARS_SD_MOSI, GPIO_FLOATING);
  gpio_set_pull_mode(CONFIG_ARS_SD_SCK, GPIO_FLOATING);
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
  esp_err_t err = sd_extcs_reset_and_cmd0(
      &card_idle, &saw_non_ff, &cs_low_stuck_warning, &precheck_diag);
  const char *cs_low_stuck_note =
      cs_low_stuck_warning
          ? " (MISO stuck high while CS asserted before CMD0; check CS path)"
          : "";
  const char *classification =
      card_idle ? "OK" : (saw_non_ff ? "WIRED_BUT_NO_RESP" : "ABSENT");
  if (!card_idle) {
    s_extcs_state =
        saw_non_ff ? SD_EXTCS_STATE_INIT_FAIL : SD_EXTCS_STATE_ABSENT;
    sd_extcs_seq_mark_state(s_extcs_state);
    ESP_LOGW(
        TAG,
        "CMD0 failed: state=%s class=%s err=%s (saw_non_ff=%d "
        "miso_precheck_all_ff=%d)%s"
        " cs_level=%d host=%d freq=%u kHz"
        " miso_high=%02X %02X %02X %02X miso_low=%02X %02X %02X %02X",
        sd_extcs_state_str(s_extcs_state), classification,
        esp_err_to_name(err), saw_non_ff, cs_low_stuck_warning,
        cs_low_stuck_note, s_cs_level, s_host_id,
        s_active_freq_khz, precheck_diag.sample_high[0],
        precheck_diag.sample_high[1], precheck_diag.sample_high[2],
        precheck_diag.sample_high[3], precheck_diag.sample_low[0],
        precheck_diag.sample_low[1], precheck_diag.sample_low[2],
        precheck_diag.sample_low[3]);
    return saw_non_ff ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_NOT_FOUND;
  }

  s_extcs_state = SD_EXTCS_STATE_IDLE_READY;
  sd_extcs_seq_mark_state(s_extcs_state);
  ESP_LOGI(TAG, "CMD0: Card entered idle state (R1=0x01) class=OK");
  s_seq_stats.cmd0_seen = true;

  uint8_t resp_r1 = 0xFF;
  bool mmc_detection_mode = false;
  int illegal_cmd55_streak = 0;

  // CMD8: check voltage range
  uint8_t resp_r7[5] = {0};
  err = sd_extcs_send_command(8, 0x000001AA, 0x87, resp_r7, sizeof(resp_r7),
                              SD_EXTCS_CMD_TIMEOUT_TICKS, NULL, true, true);
  bool sdhc_candidate = false;
  if (err == ESP_OK && resp_r7[0] == 0x01) {
    uint32_t pattern = ((uint32_t)resp_r7[1] << 24) |
                       ((uint32_t)resp_r7[2] << 16) |
                       ((uint32_t)resp_r7[3] << 8) | resp_r7[4];
    sdhc_candidate = (pattern == 0x000001AA);
    ESP_LOGI(TAG, "CMD8 OK (pattern=0x%08" PRIX32 ")", pattern);
    s_seq_stats.cmd8_seen = true;
  } else if (err == ESP_OK && resp_r7[0] == 0x05) {
    mmc_detection_mode = true;
    ESP_LOGW(TAG, "CMD8 illegal (resp=0x05). Enabling MMC/SDv1 detect mode.");
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
  bool mmc_candidate = false;
  bool mmc_ready = false;
  int64_t init_deadline = esp_timer_get_time() + SD_EXTCS_INIT_TIMEOUT_US;
  while (esp_timer_get_time() < init_deadline) {
    acmd41_attempts++;
    sd_extcs_r1_trace_t trace55 = {0};
    err = sd_extcs_send_command(55, 0x00000000, 0x65, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS, &trace55, true,
                                mmc_detection_mode);
    bool illegal_cmd55 = (resp_r1 & 0x04) != 0;
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD55 failed (attempt %d): %s", acmd41_attempts,
               esp_err_to_name(err));
    }
    if (illegal_cmd55) {
      illegal_cmd55_streak++;
      if (mmc_detection_mode && illegal_cmd55_streak >= 2) {
        mmc_candidate = true;
        card_ready = false;
        ESP_LOGW(TAG,
                 "CMD55 illegal command confirmed (%d streak, resp=0x%02X); "
                 "trying MMC",
                 illegal_cmd55_streak, resp_r1);
        break;
      }
    } else {
      illegal_cmd55_streak = 0;
    }

    if (err != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    sd_extcs_r1_trace_t trace41 = {0};
    err = sd_extcs_send_command(41, acmd41_arg, 0x77, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS, &trace41, true,
                                false);
    if (err == ESP_OK && resp_r1 == 0x00) {
      s_seq_stats.acmd41_seen = true;
      card_ready = true;
      break;
    }

    ESP_LOGI(TAG, "ACMD41 attempt %d resp=0x%02X err=%s", acmd41_attempts,
             resp_r1, esp_err_to_name(err));

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!card_ready && mmc_candidate) {
    if (mmc_candidate) {
      int cmd1_attempts = 0;
      while (esp_timer_get_time() < init_deadline) {
        cmd1_attempts++;
        err = sd_extcs_send_command(1, 0x40300000, 0x01, &resp_r1, 1,
                                    SD_EXTCS_CMD_TIMEOUT_TICKS, NULL, true,
                                    false);
        if (err == ESP_OK && resp_r1 == 0x00) {
          mmc_ready = true;
          break;
        }
        ESP_LOGI(TAG, "CMD1 attempt %d resp=0x%02X err=%s", cmd1_attempts,
                 resp_r1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      if (!mmc_ready) {
        ESP_LOGE(TAG,
                 "CMD1 (MMC init) timeout after %d attempts. Card not ready.",
                 cmd1_attempts);
        s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
        sd_extcs_seq_mark_state(s_extcs_state);
        return ESP_ERR_TIMEOUT;
      }
      card_ready = true;
      ESP_LOGI(TAG, "MMC init via CMD1 completed in %d attempt(s)",
               cmd1_attempts);
    }
  }

  if (!card_ready) {
    ESP_LOGE(
        TAG,
        "ACMD41 timeout. SD card not ready. Insert card or verify cabling.");
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    sd_extcs_seq_mark_state(s_extcs_state);
    return ESP_ERR_TIMEOUT;
  }

  if (mmc_ready) {
    ESP_LOGI(TAG, "Card initialized via MMC flow (CMD1)");
  } else {
    ESP_LOGI(TAG, "ACMD41 completed in %d attempt(s), card ready",
             acmd41_attempts);
  }

  // CMD58: read OCR with bounded retries for deterministic behavior
  uint8_t resp_r3[5] = {0};
  bool ocr_ok = false;
  int ocr_attempt = 0;
  bool skip_cmd58 = mmc_ready;
  uint32_t ocr = 0;
  bool high_capacity = false;
  if (!skip_cmd58) {
    for (int attempt = 0; attempt < SD_EXTCS_CMD58_RETRIES; ++attempt) {
      ocr_attempt = attempt + 1;
      memset(resp_r3, 0, sizeof(resp_r3));
      err = sd_extcs_send_command(58, 0x00000000, 0xFD, resp_r3,
                                  sizeof(resp_r3), SD_EXTCS_CMD_TIMEOUT_TICKS,
                                  NULL, true, false);
      if (err == ESP_OK && resp_r3[0] == 0x00) {
        s_seq_stats.cmd58_seen = true;
        ocr_ok = true;
        break;
      }
      ESP_LOGW(TAG, "CMD58 attempt %d/%d resp=0x%02X err=%s", ocr_attempt,
               SD_EXTCS_CMD58_RETRIES, resp_r3[0], esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(25));
    }
  } else {
    ocr_ok = true;
    ESP_LOGW(TAG, "CMD58 skipped (MMC path)");
  }

  if (!ocr_ok) {
    ESP_LOGE(
        TAG,
        "CMD58 failed after %d attempt(s). Insert SD card or check wiring.",
        ocr_attempt);
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    sd_extcs_seq_mark_state(s_extcs_state);
    return err != ESP_OK ? err : ESP_ERR_TIMEOUT;
  }

  if (!skip_cmd58) {
    ocr = ((uint32_t)resp_r3[1] << 24) | ((uint32_t)resp_r3[2] << 16) |
          ((uint32_t)resp_r3[3] << 8) | resp_r3[4];
    high_capacity = (ocr & (1 << 30)) != 0;
    ESP_LOGI(TAG, "OCR=0x%08" PRIX32 " (CCS=%d) (attempt=%d)", ocr,
             high_capacity, ocr_attempt);
  } else {
    ESP_LOGI(TAG, "OCR read skipped (MMC path, CCS unknown)");
  }

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
  sd_extcs_seq_reset(init_freq_khz, host_target_khz);

  ESP_LOGI(TAG, "Mounting SD (SDSPI ext-CS) init=%u kHz target=%u kHz",
           init_freq_khz, host_target_khz);

  s_extcs_state = SD_EXTCS_STATE_UNINITIALIZED;
  sd_extcs_seq_mark_state(s_extcs_state);

  // Ensure shared I2C bus primitives are ready before IO extender toggling
  if (i2c_bus_shared_init() != ESP_OK) {
    ESP_LOGE(TAG, "SD: I2C shared bus not ready");
    s_extcs_state = SD_EXTCS_STATE_INIT_FAIL;
    sd_extcs_seq_mark_state(s_extcs_state);
    return ESP_ERR_INVALID_STATE;
  }

#if CONFIG_ARS_SD_SDMMC_DEBUG_LOG
  esp_log_level_set("sdspi_host", ESP_LOG_DEBUG);
  esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
  esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
#else
  // Reduce benign warnings such as "command not supported" while keeping real
  // failures visible during normal operation.
  esp_log_level_set("sdspi_host", ESP_LOG_ERROR);
  esp_log_level_set("sdmmc_common", ESP_LOG_ERROR);
  esp_log_level_set("sdmmc_cmd", ESP_LOG_ERROR);
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
    sd_extcs_seq_mark_state(s_extcs_state);
    return ESP_ERR_INVALID_STATE;
  }
  IO_EXTENSION_IO_Mode(0xFF); // ensure outputs (push-pull)
  ESP_LOGI(TAG,
           "SD ExtCS: IOEXT outputs configured (mask=0xFF, CS=IO%d push-pull)",
           IO_EXTENSION_IO_4);
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
    ESP_LOGE(TAG,
             "SD: CS line check failed. Card will not respond until fixed.");
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

  ESP_LOGI(TAG,
           "SDSPI host=%d mode=0 dma=%d init_khz=%u (pre-CMD0 low-speed path)",
           s_host_id, SDSPI_DEFAULT_DMA, init_freq_khz);

  ret = spi_bus_initialize(s_host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret == ESP_OK) {
    s_bus_initialized = true;
  } else if (ret == ESP_ERR_INVALID_STATE) {
    if (!s_bus_initialized) {
      s_bus_initialized = true; // already initialized by previous attempt
    }
    ESP_LOGW(TAG, "SPI bus already initialized, reusing existing bus");
  } else {
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

  esp_err_t diag_ret = sd_extcs_diag_toggle_and_cmd0();
  if (diag_ret != ESP_OK) {
    ESP_LOGW(TAG, "SD diag (CS+CMD0) failed: %s", esp_err_to_name(diag_ret));
  }

  sd_extcs_force_cs_low_miso_debug();

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
        ESP_LOGW(
            TAG,
            "Card max_freq_khz=%u kHz matches init clock; honoring host target",
            s_card->max_freq_khz);
      }
    }

    esp_err_t clk_ret =
        sd_extcs_raise_clock(host_target_khz, card_limit_khz, init_freq_khz);
    ESP_LOGI(TAG,
             "SD clock summary: init=%u kHz -> active=%u kHz (target=%u kHz, "
             "card_max=%u kHz)",
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
    sd_extcs_seq_mark_state(s_extcs_state);
    sd_extcs_free_bus_if_idle();
  }

  sd_extcs_seq_mark_state(s_extcs_state);
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

    // Remove cleanup device and free bus for deterministic re-mount attempts
    sd_extcs_free_bus_if_idle();
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
