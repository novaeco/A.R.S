/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_ARS_TOUCH_SWAP_XY
#define CONFIG_ARS_TOUCH_SWAP_XY 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_X
#define CONFIG_ARS_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_Y
#define CONFIG_ARS_TOUCH_MIRROR_Y 0
#endif

/* Calibration Defaults */
#ifndef CONFIG_ARS_TOUCH_OFFSET_X
#define CONFIG_ARS_TOUCH_OFFSET_X 0
#endif
#ifndef CONFIG_ARS_TOUCH_OFFSET_Y
#define CONFIG_ARS_TOUCH_OFFSET_Y 0
#endif
#ifndef CONFIG_ARS_TOUCH_SCALE_X
#define CONFIG_ARS_TOUCH_SCALE_X 1000
#endif
#ifndef CONFIG_ARS_TOUCH_SCALE_Y
#define CONFIG_ARS_TOUCH_SCALE_Y 1000
#endif

#include "gpio.h"
#include "i2c.h"
#include "i2c_bus_shared.h"
#include "io_extension.h"
#include <rom/ets_sys.h>

#include "gt911.h"

#define GT911_RAW_MARGIN 64
#define GT911_DEBUG_DUMP_INTERVAL_US 500000

#if CONFIG_ARS_TOUCH_GT911_LAYOUT_A
#define GT911_LAYOUT_FORCED 1
#elif CONFIG_ARS_TOUCH_GT911_LAYOUT_B
#define GT911_LAYOUT_FORCED 2
#else
#define GT911_LAYOUT_FORCED 0
#endif

static inline uint16_t u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static const char *TAG = "GT911";
static TaskHandle_t s_gt911_irq_task = NULL;
static volatile uint32_t s_gt911_irq_total = 0;
static volatile uint32_t s_gt911_irq_storm = 0;
static volatile uint32_t s_gt911_irq_empty = 0;
static volatile uint32_t s_gt911_i2c_errors = 0;
static volatile uint32_t s_gt911_invalid_points = 0;
static volatile uint32_t s_gt911_clamped_points = 0;
static int64_t s_last_irq_us = 0;
static int64_t s_spurious_block_until_us = 0;
static int64_t s_next_ioext_reset_after_us = 0;
static int s_gt911_int_gpio = -1;
static uint16_t s_gt911_last_raw_x = 0;
static uint16_t s_gt911_last_raw_y = 0;
static uint16_t s_gt911_last_invalid_x = 0;
static uint16_t s_gt911_last_invalid_y = 0;
static uint16_t s_gt911_last_clamped_x = 0;
static uint16_t s_gt911_last_clamped_y = 0;
static uint32_t s_gt911_consecutive_errors = 0;
static int64_t s_empty_window_start_us = 0;
static uint32_t s_empty_window_count = 0;
static bool s_poll_mode = false;
static int64_t s_poll_mode_until_us = 0;
static uint32_t s_poll_interval_ms = GT911_POLL_INTERVAL_MS;
static portMUX_TYPE s_gt911_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_last_touch_down = false;
static portMUX_TYPE s_gt911_error_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_gt911_use_ioext_reset = false;
static uint8_t s_gt911_ioext_reset_pin = IO_EXTENSION_IO_1;

typedef struct {
  uint32_t i2c_errors;
  uint32_t poll_timeouts;
  uint32_t last_reported_i2c_errors;
  uint32_t last_reported_poll_timeouts;
  int64_t last_log_us;
} gt911_error_metrics_t;

static gt911_error_metrics_t s_gt911_errors = {0};
static bool s_config_dumped_once = false;

#define GT911_EMPTY_BURST_LIMIT 10
#define GT911_EMPTY_WINDOW_US 200000
#define GT911_POLL_DURATION_US 2000000
#define GT911_POLL_INTERVAL_MS_FALLBACK 60
#define GT911_IRQ_REENABLE_DELAY_US 3000
#define GT911_I2C_RESET_THRESHOLD 5

static inline void gt911_enable_irq_guarded(void) {
  if (s_gt911_int_gpio >= 0 && !s_poll_mode) {
    ets_delay_us(GT911_IRQ_REENABLE_DELAY_US);
    gpio_intr_enable(s_gt911_int_gpio);
  }
}

static inline void gt911_enter_poll_mode(uint32_t interval_ms, int64_t duration_us) {
  s_poll_mode = true;
  s_poll_interval_ms = interval_ms;
  s_poll_mode_until_us = duration_us > 0 ? (esp_timer_get_time() + duration_us)
                                          : INT64_MAX;
}

static inline void gt911_mark_i2c_error(void) {
  portENTER_CRITICAL(&s_gt911_error_lock);
  s_gt911_errors.i2c_errors++;
  portEXIT_CRITICAL(&s_gt911_error_lock);

  portENTER_CRITICAL(&s_gt911_stats_lock);
  s_gt911_i2c_errors++;
  portEXIT_CRITICAL(&s_gt911_stats_lock);
}

static inline void gt911_mark_poll_timeout(void) {
  portENTER_CRITICAL(&s_gt911_error_lock);
  s_gt911_errors.poll_timeouts++;
  portEXIT_CRITICAL(&s_gt911_error_lock);
}

#if !CONFIG_ARS_TOUCH_DISABLE_LEGACY_CAL
static uint16_t s_raw_max_x_seen = CONFIG_ARS_TOUCH_X_MAX;
static uint16_t s_raw_max_y_seen = CONFIG_ARS_TOUCH_Y_MAX;
#endif

#if CONFIG_GT911_DEBUG_DUMP
static int64_t s_last_debug_dump_us = 0;
static void gt911_debug_dump_frame(uint8_t status, const uint8_t *buf,
                                   size_t len) {
  int64_t now = esp_timer_get_time();
  if ((now - s_last_debug_dump_us) < GT911_DEBUG_DUMP_INTERVAL_US) {
    return;
  }
  s_last_debug_dump_us = now;

  char hex[3 * 64] = {0};
  size_t hex_len = 0;
  size_t capped_len = len > 21 ? 21 : len; // 21 bytes -> 63 chars
  for (size_t i = 0; i < capped_len && hex_len + 3 < sizeof(hex); ++i) {
    hex_len += snprintf(hex + hex_len, sizeof(hex) - hex_len, "%02X ", buf[i]);
  }
  ESP_LOGI(TAG, "GT911 dump status=0x%02X len=%u bytes: %s", status,
           (unsigned int)len, hex);

  if (len >= 8) {
    uint8_t track_id = buf[0];
    uint16_t raw_x = u16_le(&buf[1]);
    uint16_t raw_y = u16_le(&buf[3]);
    uint16_t strength = u16_le(&buf[5]);
    ESP_LOGI(TAG,
             "GT911 decode id=%u raw=(%u,%u) strength=%u touch_cnt=%u max=(%d,%d)",
             track_id, raw_x, raw_y, strength, status & 0x0F,
             CONFIG_ARS_TOUCH_X_MAX, CONFIG_ARS_TOUCH_Y_MAX);
  }
}
#else
static inline void gt911_debug_dump_frame(uint8_t status, const uint8_t *buf,
                                          size_t len) {
  (void)status;
  (void)buf;
  (void)len;
}
#endif

static inline uint16_t gt911_apply_calibration(uint16_t raw, uint16_t max_value,
                                               int offset, int scale,
                                               bool *clamped,
                                               bool is_x_axis) {
#if CONFIG_ARS_TOUCH_DISABLE_LEGACY_CAL
  (void)offset;
  (void)scale;
  (void)is_x_axis;

  uint16_t val = raw;
  if (val >= max_value) {
    val = max_value - 1;
    if (clamped) {
      *clamped = true;
    }
  }
  return val;
#else
  int32_t val = (int32_t)raw + offset;
  val = (val * scale + 500) / 1000; // scale is milli-units

  // When raw data is dramatically larger than panel resolution, perform a
  // dynamic normalization using the largest raw value observed for the axis.
  if (val >= (int32_t)max_value) {
    uint16_t *raw_max_seen = is_x_axis ? &s_raw_max_x_seen : &s_raw_max_y_seen;
    if (raw > *raw_max_seen) {
      *raw_max_seen = raw;
    }

    if (*raw_max_seen == 0) {
      *raw_max_seen = max_value;
    }

    val = ((int32_t)raw * (int32_t)max_value) / (int32_t)(*raw_max_seen);
  }

  if (val < 0) {
    val = 0;
    if (clamped) {
      *clamped = true;
    }
  }
  if (val >= (int32_t)max_value) {
    val = (int32_t)max_value - 1;
    if (clamped) {
      *clamped = true;
    }
  }

  return (uint16_t)val;
#endif
}

/* GT911 registers */
#define ESP_LCD_TOUCH_GT911_READ_KEY_REG (0x8093)
#define ESP_LCD_TOUCH_GT911_STATUS_REG (0x814E)
#define ESP_LCD_TOUCH_GT911_POINTS_REG (0x8150)
#define ESP_LCD_TOUCH_GT911_CONFIG_REG (0x8047)
#define ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG (0x8140)
#define ESP_LCD_TOUCH_GT911_ENTER_SLEEP (0x8040)

/* GT911 Config registers for resolution */
#define GT911_REG_X_OUTPUT_MAX_L (0x8048)
#define GT911_REG_X_OUTPUT_MAX_H (0x8049)
#define GT911_REG_Y_OUTPUT_MAX_L (0x804A)
#define GT911_REG_Y_OUTPUT_MAX_H (0x804B)
#define GT911_REG_CONFIG_FRESH (0x8100)
#define GT911_REG_CONFIG_CHKSUM (0x80FF)

/* Fix constants */
#define GT911_POST_IRQ_DELAY_MS 3 // Delay after IRQ before reading
#define GT911_EMPTY_RETRY_COUNT 3 // Retries when touch_cnt=0 but IRQ fired
#define GT911_RETRY_DELAY_MS 2    // Delay between retries

/* GT911 support key num */
#define ESP_GT911_TOUCH_MAX_BUTTONS (4)

esp_lcd_touch_handle_t tp_handle = NULL; // Declare a handle for the touch panel
/*******************************************************************************
 * Function definitions
 *******************************************************************************/
static esp_err_t esp_lcd_touch_gt911_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_gt911_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x,
                                       uint16_t *y, uint16_t *strength,
                                       uint8_t *point_num,
                                       uint8_t max_point_num);
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gt911_get_button_state(esp_lcd_touch_handle_t tp,
                                                      uint8_t n,
                                                      uint8_t *state);
#endif
static esp_err_t esp_lcd_touch_gt911_del(esp_lcd_touch_handle_t tp);
static void gt911_irq_task(void *arg);
static void gt911_gpio_isr_handler(void *arg);
static esp_err_t gt911_reset_via_ioext(void);

/* I2C read/write */
static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg,
                                      uint8_t *data, uint8_t len);
static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg,
                                       uint8_t data);

/* GT911 resolution programming */
static esp_err_t gt911_update_config(esp_lcd_touch_handle_t tp);

/* GT911 reset */
static esp_err_t touch_gt911_reset(esp_lcd_touch_handle_t tp);
/* Read status and config register */
static esp_err_t touch_gt911_read_cfg(esp_lcd_touch_handle_t tp);

/* GT911 enter/exit sleep mode */
static esp_err_t esp_lcd_touch_gt911_enter_sleep(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gt911_exit_sleep(esp_lcd_touch_handle_t tp);

/*******************************************************************************
 * Public API functions
 *******************************************************************************/

esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_touch_config_t *config,
                                      esp_lcd_touch_handle_t *out_touch) {
  esp_err_t ret = ESP_OK;

  assert(io != NULL);
  assert(config != NULL);
  assert(out_touch != NULL);

  /* Prepare main structure */
  esp_lcd_touch_handle_t esp_lcd_touch_gt911 =
      heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(esp_lcd_touch_gt911, ESP_ERR_NO_MEM, err, TAG,
                    "no mem for GT911 controller");

  /* Communication interface */
  esp_lcd_touch_gt911->io = io;

  /* Only supported callbacks are set */
  esp_lcd_touch_gt911->read_data = esp_lcd_touch_gt911_read_data;
  esp_lcd_touch_gt911->get_xy = esp_lcd_touch_gt911_get_xy;
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
  esp_lcd_touch_gt911->get_button_state = esp_lcd_touch_gt911_get_button_state;
#endif
  esp_lcd_touch_gt911->del = esp_lcd_touch_gt911_del;
  esp_lcd_touch_gt911->enter_sleep = esp_lcd_touch_gt911_enter_sleep;
  esp_lcd_touch_gt911->exit_sleep = esp_lcd_touch_gt911_exit_sleep;

  /* Mutex */
  esp_lcd_touch_gt911->data.lock.owner = portMUX_FREE_VAL;

  /* Save config */
  memcpy(&esp_lcd_touch_gt911->config, config, sizeof(esp_lcd_touch_config_t));
  esp_lcd_touch_io_gt911_config_t *gt911_config =
      (esp_lcd_touch_io_gt911_config_t *)
          esp_lcd_touch_gt911->config.driver_data;

  /* Prepare pin for touch controller reset */
  if (esp_lcd_touch_gt911->config.rst_gpio_num != GPIO_NUM_NC) {
    const gpio_config_t rst_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.rst_gpio_num)};
    ret = gpio_config(&rst_gpio_config);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
  }

  if (gt911_config && esp_lcd_touch_gt911->config.rst_gpio_num != GPIO_NUM_NC &&
      esp_lcd_touch_gt911->config.int_gpio_num != GPIO_NUM_NC) {
    /* Prepare pin for touch controller int */
    const gpio_config_t int_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 1, // Enable internal pull-up
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num),
    };
    ret = gpio_config(&int_gpio_config);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

    /* Prepare pin for touch controller reset */
    const gpio_config_t rst_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 1, // Enable internal pull-up
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.rst_gpio_num),
    };
    ret = gpio_config(&rst_gpio_config);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

    /* Reset Sequence */
    // 1. Hold RST low to reset
    ESP_RETURN_ON_ERROR(
        gpio_set_level(esp_lcd_touch_gt911->config.rst_gpio_num, 0), TAG,
        "GPIO set level error!");
    vTaskDelay(pdMS_TO_TICKS(20)); // Wait >10ms

    /* Select I2C addr - Priority 0x14 (GPIO 0) */
    uint32_t gpio_level = 0; // 0x14 (default/priority)

    // Check if we explicitly wanted 0x5D (Backup), otherwise stick to 0x14
    if (ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP == gt911_config->dev_addr) {
      gpio_level = 1; // 0x5D
    }

    ESP_RETURN_ON_ERROR(
        gpio_set_level(esp_lcd_touch_gt911->config.int_gpio_num, gpio_level),
        TAG, "GPIO set level error!");

    // 2. Release RST (High)
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(
        gpio_set_level(esp_lcd_touch_gt911->config.rst_gpio_num, 1), TAG,
        "GPIO set level error!");

    // 3. Wait for booting (Hold INT state >50ms)
    vTaskDelay(pdMS_TO_TICKS(60));

    // 4. Release INT (Input/Floating)
    gpio_set_direction(esp_lcd_touch_gt911->config.int_gpio_num,
                       GPIO_MODE_INPUT);
  } else {
    // Info log to reflect external reset/INT wiring
    ESP_LOGI(TAG,
             "Using external reset (IO expander) and INT GPIO=%d, addr=0x%02X",
             (int)esp_lcd_touch_gt911->config.int_gpio_num,
             gt911_config ? gt911_config->dev_addr : 0);
  }

  bool int_configured = esp_lcd_touch_gt911->config.int_gpio_num != GPIO_NUM_NC;
  if (int_configured) {
    const gpio_config_t int_gpio_config = {
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num)};
    ret = gpio_config(&int_gpio_config);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
      goto err;
    }

    ret = gpio_isr_handler_add(esp_lcd_touch_gt911->config.int_gpio_num,
                               gt911_gpio_isr_handler, esp_lcd_touch_gt911);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "ISR handler add failed (%s), using polling fallback",
               esp_err_to_name(ret));
      int_configured = false;
    }
  }

  /* Read status and config info */
  ret = touch_gt911_read_cfg(esp_lcd_touch_gt911);
  ESP_GOTO_ON_ERROR(ret, err, TAG, "GT911 init failed");

  // Ensure shared I2C mutex is available
  ret = i2c_bus_shared_init();
  ESP_GOTO_ON_ERROR(ret, err, TAG, "GT911 shared I2C init failed");

  // Create IRQ task to handle I2C reads outside ISR or run polling fallback
  static bool task_created = false;
  if (!task_created) {
    s_gt911_int_gpio = int_configured ? esp_lcd_touch_gt911->config.int_gpio_num
                                      : -1;
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        gt911_irq_task, "gt911_irq", 4096, esp_lcd_touch_gt911, 5,
        &s_gt911_irq_task, tskNO_AFFINITY);
    if (task_ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create GT911 IRQ task");
      ret = ESP_FAIL;
      goto err;
    }
    task_created = true;
  }

  if (!int_configured) {
    ESP_LOGW(TAG, "Touch INT not available -> enabling bounded polling mode");
    gt911_enter_poll_mode(GT911_POLL_INTERVAL_MS_FALLBACK, 0);
  }

err:
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error (0x%x)! Touch controller GT911 initialization failed!",
             ret);
    if (esp_lcd_touch_gt911) {
      esp_lcd_touch_gt911_del(esp_lcd_touch_gt911);
    }
  }

  *out_touch = esp_lcd_touch_gt911;

  return ret;
}

static esp_err_t esp_lcd_touch_gt911_enter_sleep(esp_lcd_touch_handle_t tp) {
  esp_err_t err =
      touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_ENTER_SLEEP, 0x05);
  ESP_RETURN_ON_ERROR(err, TAG, "Enter Sleep failed!");

  return ESP_OK;
}

static esp_err_t esp_lcd_touch_gt911_exit_sleep(esp_lcd_touch_handle_t tp) {
  esp_err_t ret;
  esp_lcd_touch_handle_t esp_lcd_touch_gt911 = tp;

  if (esp_lcd_touch_gt911->config.int_gpio_num != GPIO_NUM_NC) {
    const gpio_config_t int_gpio_config_high = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num)};
    ret = gpio_config(&int_gpio_config_high);
    ESP_RETURN_ON_ERROR(ret, TAG, "High GPIO config failed");
    gpio_set_level(esp_lcd_touch_gt911->config.int_gpio_num, 1);

    vTaskDelay(pdMS_TO_TICKS(5));

    const gpio_config_t int_gpio_config_float = {
        .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num)};
    ret = gpio_config(&int_gpio_config_float);
    ESP_RETURN_ON_ERROR(ret, TAG, "Float GPIO config failed");
  }

  return ESP_OK;
}

// static esp_err_t gt911_ensure_io_handle(esp_lcd_touch_handle_t tp);

static inline bool gt911_should_log(int64_t *last_log_us, int64_t interval_us) {
  int64_t now = esp_timer_get_time();
  if ((now - *last_log_us) >= interval_us) {
    *last_log_us = now;
    return true;
  }
  return false;
}

typedef struct {
  uint16_t raw_x;
  uint16_t raw_y;
  uint16_t x;
  uint16_t y;
  uint16_t strength;
  bool clamped;
} gt911_point_decoded_t;

static esp_err_t esp_lcd_touch_gt911_read_data(esp_lcd_touch_handle_t tp) {
  esp_err_t err = ESP_OK;
  uint8_t status = 0;
  uint8_t touch_cnt = 0;

  assert(tp != NULL);

  err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_STATUS_REG, &status, 1);
  if (err != ESP_OK) {
    s_gt911_consecutive_errors++;
    s_gt911_i2c_errors++;
    if (s_gt911_consecutive_errors > GT911_I2C_RESET_THRESHOLD) {
      ESP_LOGE(TAG, "Too many I2C errors (%" PRIu32 "), resetting GT911...",
               s_gt911_consecutive_errors);
      esp_err_t reset_ret = touch_gt911_reset(tp);
      if (reset_ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 reset failed: %s", esp_err_to_name(reset_ret));
      }
      s_gt911_consecutive_errors = 0;
    }
    return err;
  }

  if (s_gt911_consecutive_errors > 0) {
    ESP_LOGI(TAG, "I2C recovered after %" PRIu32 " errors",
             s_gt911_consecutive_errors);
    s_gt911_consecutive_errors = 0;
  }

  const bool data_ready = (status & 0x80) != 0;
  touch_cnt = status & 0x0F;

  if (!data_ready) {
    return ESP_OK;
  }

  static int32_t filt_x = -1;
  static int32_t filt_y = -1;

  if (touch_cnt == 0) {
    esp_err_t clr_ret =
        touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_STATUS_REG, 0x00);
    if (clr_ret != ESP_OK) {
      s_gt911_i2c_errors++;
      ESP_LOGW(TAG, "Failed to clear GT911 status: %s",
               esp_err_to_name(clr_ret));
    }

    filt_x = -1;
    filt_y = -1;

    s_gt911_irq_empty++;
    s_gt911_irq_storm++;
    int64_t now = esp_timer_get_time();

    if (s_spurious_block_until_us < now) {
      s_spurious_block_until_us = now + 20000;
    }
    if (s_empty_window_start_us == 0 ||
        (now - s_empty_window_start_us) > GT911_EMPTY_WINDOW_US) {
      s_empty_window_start_us = now;
      s_empty_window_count = 0;
    }
    s_empty_window_count++;
    if (s_empty_window_count >= GT911_EMPTY_BURST_LIMIT && !s_poll_mode) {
      gt911_enter_poll_mode(GT911_POLL_INTERVAL_MS, GT911_POLL_DURATION_US);
      static int64_t last_poll_log = 0;
      if (gt911_should_log(&last_poll_log, 1000000)) {
        ESP_LOGW(TAG,
                 "Empty IRQ burst detected (%" PRIu32 "), switching to polling",
                 s_empty_window_count);
      }
    }

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    s_last_touch_down = false;
    return ESP_OK;
  }

  if (touch_cnt > ESP_LCD_TOUCH_MAX_POINTS) {
    touch_cnt = ESP_LCD_TOUCH_MAX_POINTS;
  }

  uint8_t point_buf[8 * ESP_LCD_TOUCH_MAX_POINTS] = {0};
  size_t read_len = touch_cnt * 8;
  if (read_len > sizeof(point_buf)) {
    read_len = sizeof(point_buf);
  }

  err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_POINTS_REG, point_buf,
                             read_len);
  if (err != ESP_OK) {
    s_gt911_i2c_errors++;
    s_gt911_consecutive_errors++;
    static int64_t last_err_log = 0;
    if (gt911_should_log(&last_err_log, 1000000)) {
      ESP_LOGW(TAG, "I2C read points error: %s", esp_err_to_name(err));
    }
    return err;
  }

  gt911_debug_dump_frame(status, point_buf, read_len);

  gt911_point_decoded_t decoded[ESP_LCD_TOUCH_MAX_POINTS] = {0};
  uint16_t first_raw_x = 0;
  uint16_t first_raw_y = 0;
  uint32_t invalid_points = 0;
  uint32_t clamped_points = 0;
  uint16_t last_invalid_x = 0;
  uint16_t last_invalid_y = 0;
  uint16_t last_clamped_x = 0;
  uint16_t last_clamped_y = 0;
  size_t stored_points = 0;
  bool first_raw_set = false;

  for (size_t i = 0; i < touch_cnt; i++) {
    const uint8_t *p = &point_buf[i * 8];

    uint16_t xa = u16_le(&p[0]);
    uint16_t ya = u16_le(&p[2]);
    uint16_t size_a = u16_le(&p[4]);
    uint16_t xb = u16_le(&p[1]);
    uint16_t yb = u16_le(&p[3]);
    uint16_t size_b = u16_le(&p[5]);

    bool a_ok = xa <= (tp->config.x_max + CONFIG_ARS_TOUCH_GT911_LAYOUT_MARGIN) &&
                ya <= (tp->config.y_max + CONFIG_ARS_TOUCH_GT911_LAYOUT_MARGIN);
    bool b_ok = xb <= (tp->config.x_max + CONFIG_ARS_TOUCH_GT911_LAYOUT_MARGIN) &&
                yb <= (tp->config.y_max + CONFIG_ARS_TOUCH_GT911_LAYOUT_MARGIN);

    const bool forced_layout = (GT911_LAYOUT_FORCED != 0);
    bool use_layout_a = true;
    if (GT911_LAYOUT_FORCED == 1) {
      use_layout_a = true;
    } else if (GT911_LAYOUT_FORCED == 2) {
      use_layout_a = false;
    } else if (b_ok && !a_ok) {
      use_layout_a = false;
    } else {
      use_layout_a = a_ok || (!a_ok && !b_ok);
    }

    uint16_t raw_x = use_layout_a ? xa : xb;
    uint16_t raw_y = use_layout_a ? ya : yb;
    bool plausible = (use_layout_a ? a_ok : b_ok);
    if (!plausible && !forced_layout) {
      invalid_points++;
      last_invalid_x = raw_x;
      last_invalid_y = raw_y;
      continue;
    }
    if (!plausible && forced_layout) {
      invalid_points++;
      last_invalid_x = raw_x;
      last_invalid_y = raw_y;
    }

    bool clamped = false;
    uint16_t cal_x = gt911_apply_calibration(
        raw_x, tp->config.x_max, CONFIG_ARS_TOUCH_OFFSET_X,
        CONFIG_ARS_TOUCH_SCALE_X, &clamped, true);
    uint16_t cal_y = gt911_apply_calibration(
        raw_y, tp->config.y_max, CONFIG_ARS_TOUCH_OFFSET_Y,
        CONFIG_ARS_TOUCH_SCALE_Y, &clamped, false);

    if (clamped) {
      clamped_points++;
      last_clamped_x = raw_x;
      last_clamped_y = raw_y;
    }

    if (!first_raw_set) {
      first_raw_set = true;
      first_raw_x = raw_x;
      first_raw_y = raw_y;
    }

    if (filt_x < 0 || filt_y < 0) {
      filt_x = cal_x;
      filt_y = cal_y;
    } else {
      filt_x = (filt_x + cal_x) / 2;
      filt_y = (filt_y + cal_y) / 2;
    }

    decoded[stored_points].raw_x = raw_x;
    decoded[stored_points].raw_y = raw_y;
    decoded[stored_points].x = (uint16_t)filt_x;
    decoded[stored_points].y = (uint16_t)filt_y;
    decoded[stored_points].strength = use_layout_a ? size_a : size_b;
    decoded[stored_points].clamped = clamped;
    stored_points++;
  }

  portENTER_CRITICAL(&tp->data.lock);
  tp->data.points = 0;
  for (size_t i = 0; i < stored_points; i++) {
    tp->data.coords[i].x = decoded[i].x;
    tp->data.coords[i].y = decoded[i].y;
    tp->data.coords[i].strength = decoded[i].strength;
  }
  tp->data.points = stored_points;
  portEXIT_CRITICAL(&tp->data.lock);

  portENTER_CRITICAL(&s_gt911_stats_lock);
  if (stored_points > 0) {
    s_gt911_last_raw_x = first_raw_x;
    s_gt911_last_raw_y = first_raw_y;
    s_empty_window_count = 0;
    s_empty_window_start_us = 0;
    s_gt911_irq_storm = 0;
    s_spurious_block_until_us = 0;
  } else {
    s_gt911_last_raw_x = 0;
    s_gt911_last_raw_y = 0;
  }

  if (invalid_points > 0) {
    s_gt911_invalid_points += invalid_points;
    s_gt911_last_invalid_x = last_invalid_x;
    s_gt911_last_invalid_y = last_invalid_y;
  }

  if (clamped_points > 0) {
    s_gt911_clamped_points += clamped_points;
    s_gt911_last_clamped_x = last_clamped_x;
    s_gt911_last_clamped_y = last_clamped_y;
  }
  portEXIT_CRITICAL(&s_gt911_stats_lock);

  esp_err_t clr_ret =
      touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_STATUS_REG, 0x00);
  if (clr_ret != ESP_OK) {
    s_gt911_i2c_errors++;
    ESP_LOGW(TAG, "Failed to clear GT911 status: %s", esp_err_to_name(clr_ret));
  }

  static int64_t last_invalid_log = 0;
  static int64_t last_clamp_log = 0;
  if (invalid_points > 0 && gt911_should_log(&last_invalid_log, 1000000)) {
    ESP_LOGW(TAG,
             "Dropped %" PRIu32 " invalid points (last raw=%u,%u margin=%d)",
             invalid_points, last_invalid_x, last_invalid_y,
             CONFIG_ARS_TOUCH_GT911_LAYOUT_MARGIN);
  }
  if (clamped_points > 0 && gt911_should_log(&last_clamp_log, 1000000)) {
    ESP_LOGI(TAG, "Clamped %" PRIu32 " points (last raw=%u,%u)",
             clamped_points, last_clamped_x, last_clamped_y);
  }

  s_last_touch_down = (stored_points > 0);

  return ESP_OK;
}

static bool esp_lcd_touch_gt911_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x,
                                       uint16_t *y, uint16_t *strength,
                                       uint8_t *point_num,
                                       uint8_t max_point_num) {
  assert(tp != NULL);
  assert(x != NULL);
  assert(y != NULL);
  assert(point_num != NULL);
  assert(max_point_num > 0);

  portENTER_CRITICAL(&tp->data.lock);

  /* Count of points */
  *point_num =
      (tp->data.points > max_point_num ? max_point_num : tp->data.points);

  for (size_t i = 0; i < *point_num; i++) {
    x[i] = tp->data.coords[i].x;
    y[i] = tp->data.coords[i].y;

    if (strength) {
      strength[i] = tp->data.coords[i].strength;
    }
  }

  /* Invalidate */
  tp->data.points = 0;

  portEXIT_CRITICAL(&tp->data.lock);

  return (*point_num > 0);
}

// ... (get_button_state, del, init maintained outside this block) ...

#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gt911_get_button_state(esp_lcd_touch_handle_t tp,
                                                      uint8_t n,
                                                      uint8_t *state) {
  esp_err_t err = ESP_OK;
  assert(tp != NULL);
  assert(state != NULL);

  *state = 0;

  portENTER_CRITICAL(&tp->data.lock);

  if (n > tp->data.buttons) {
    err = ESP_ERR_INVALID_ARG;
  } else {
    *state = tp->data.button[n].status;
  }

  portEXIT_CRITICAL(&tp->data.lock);

  return err;
}
#endif

void gt911_get_stats(gt911_stats_t *stats) {
  if (!stats) {
    return;
  }

  portENTER_CRITICAL(&s_gt911_stats_lock);
  stats->irq_total = s_gt911_irq_total;
  stats->empty_irqs = s_gt911_irq_empty;
  stats->i2c_errors = s_gt911_i2c_errors;
  stats->poll_timeouts = s_gt911_errors.poll_timeouts;
  stats->invalid_points = s_gt911_invalid_points;
  stats->clamped_points = s_gt911_clamped_points;
  stats->polling_active = s_poll_mode;
  stats->last_raw_x = s_gt911_last_raw_x;
  stats->last_raw_y = s_gt911_last_raw_y;
  stats->last_invalid_x = s_gt911_last_invalid_x;
  stats->last_invalid_y = s_gt911_last_invalid_y;
  stats->last_clamped_x = s_gt911_last_clamped_x;
  stats->last_clamped_y = s_gt911_last_clamped_y;
  portEXIT_CRITICAL(&s_gt911_stats_lock);
}

void gt911_reset_stats(void) {
  portENTER_CRITICAL(&s_gt911_stats_lock);
  s_gt911_irq_total = 0;
  s_gt911_irq_empty = 0;
  s_gt911_irq_storm = 0;
  s_gt911_i2c_errors = 0;
  s_gt911_errors.i2c_errors = 0;
  s_gt911_errors.poll_timeouts = 0;
  s_gt911_last_raw_x = 0;
  s_gt911_last_raw_y = 0;
  s_gt911_invalid_points = 0;
  s_gt911_clamped_points = 0;
  s_gt911_last_invalid_x = 0;
  s_gt911_last_invalid_y = 0;
  s_gt911_last_clamped_x = 0;
  s_gt911_last_clamped_y = 0;
  portEXIT_CRITICAL(&s_gt911_stats_lock);
}

static esp_err_t esp_lcd_touch_gt911_del(esp_lcd_touch_handle_t tp) {
  assert(tp != NULL);

  /* Reset GPIO pin settings */
  if (tp->config.int_gpio_num != GPIO_NUM_NC) {
    gpio_reset_pin(tp->config.int_gpio_num);
    if (tp->config.interrupt_callback) {
      gpio_isr_handler_remove(tp->config.int_gpio_num);
    }
  }

  /* Reset GPIO pin settings */
  if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
    gpio_reset_pin(tp->config.rst_gpio_num);
  }

  free(tp);

  return ESP_OK;
}

static esp_err_t gt911_reset_via_ioext(void) {
  if (!s_gt911_use_ioext_reset) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  int64_t now = esp_timer_get_time();
  if (now < s_next_ioext_reset_after_us) {
    ESP_LOGW(TAG, "GT911 reset skipped: IOEXT backoff active");
    return ESP_ERR_TIMEOUT;
  }

  if (!IO_EXTENSION_Is_Initialized()) {
    ESP_LOGE(TAG, "IO extension not ready for GT911 reset");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = IO_EXTENSION_Output(s_gt911_ioext_reset_pin, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT touch reset low failed: %s", esp_err_to_name(ret));
    s_next_ioext_reset_after_us = esp_timer_get_time() + 500000; // 500ms backoff
    return ret;
  }
  vTaskDelay(pdMS_TO_TICKS(20));

  ret = IO_EXTENSION_Output(s_gt911_ioext_reset_pin, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT touch reset high failed: %s", esp_err_to_name(ret));
    s_next_ioext_reset_after_us = esp_timer_get_time() + 500000; // 500ms backoff
    return ret;
  }

  vTaskDelay(pdMS_TO_TICKS(60));
  s_next_ioext_reset_after_us = esp_timer_get_time() + 100000; // 100ms guard
  return ESP_OK;
}

// Cleanup and Self-Healing removed as the I2C bus is now persistent
// (Singleton/Shared) and recovery is non-destructive.

// Function to initialize the GT911 touch controller
esp_lcd_touch_handle_t touch_gt911_init() {
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  const esp_lcd_panel_io_i2c_config_t tp_io_config =
      ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

  // Initialize Shared I2C Bus First (single owner)
  esp_err_t ret = i2c_bus_shared_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C Bus Init Failed: %s", esp_err_to_name(ret));
    return NULL;
  }

  i2c_master_bus_handle_t bus_handle = i2c_bus_shared_get_handle();
  if (!bus_handle) {
    ESP_LOGE(TAG, "I2C Bus Init Failed (shared bus not ready)");
    return NULL;
  }
  ret = DEV_I2C_Init_Bus(&bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C Bus Init Failed");
    return NULL;
  }

  // Ensure IO Extension is ready via safe init/handle
  // Ensure IO Extension is ready via safe init
  esp_err_t ioext_ret = IO_EXTENSION_Init();
  if (ioext_ret != ESP_OK) {
    ESP_LOGE(TAG, "IO extension init failed: %s", esp_err_to_name(ioext_ret));
    return NULL;
  }

  s_gt911_use_ioext_reset = true;
  s_gt911_ioext_reset_pin = IO_EXTENSION_IO_1;

  // 2. Perform Robust Reset Sequence (Waveshare specific)
  const int int_pin = EXAMPLE_PIN_NUM_TOUCH_INT;
  const int rst_pin_io = IO_EXTENSION_IO_1;

  ESP_LOGI(
      TAG,
      "Starting GT911 Reset Sequence (IRQ=GPIO%d, RST=IOEXT%d addr=0x%02X)",
      int_pin, rst_pin_io, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);

  gpio_config_t int_out_cfg = {
      .pin_bit_mask = BIT64(int_pin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = 1,
      .pull_down_en = 0,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&int_out_cfg);
  gpio_set_level(int_pin, 0);

  // Step B: Set RST Low via IO Extension
  esp_err_t io_ret = IO_EXTENSION_Output(rst_pin_io, 0);
  if (io_ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT touch reset low failed: %s", esp_err_to_name(io_ret));
    return NULL;
  }
  vTaskDelay(pdMS_TO_TICKS(20));

  // Step C: Set RST High
  io_ret = IO_EXTENSION_Output(rst_pin_io, 1);
  if (io_ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT touch reset high failed: %s", esp_err_to_name(io_ret));
    return NULL;
  }
  vTaskDelay(pdMS_TO_TICKS(60)); // Wait >50ms

  // Step D: Set INT Input (End of address selection)
  gpio_config_t int_in_cfg = {
      .pin_bit_mask = BIT64(int_pin),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = 1,
      .pull_down_en = 0,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&int_in_cfg);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 3. I2C Address Scan / Validation
  uint8_t current_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
  // Sanitize Address immediately
  uint8_t sanitized_addr = 0;
  esp_err_t sanitize_ret = DEV_I2C_SanitizeAddr(current_addr, &sanitized_addr);
  if (sanitize_ret != ESP_OK) {
    ESP_LOGE(TAG, "Invalid GT911 I2C address 0x%02X: %s", current_addr,
             esp_err_to_name(sanitize_ret));
    return NULL;
  }
  current_addr = sanitized_addr;

  ESP_LOGI(TAG, "Initialize I2C panel IO at address 0x%02X", current_addr);
  esp_lcd_panel_io_i2c_config_t config_with_addr = tp_io_config;
  config_with_addr.dev_addr = current_addr;

  // Probe before creating IO handle to avoid aborting the boot on absent device
  if (DEV_I2C_Probe(bus_handle, config_with_addr.dev_addr) != ESP_OK) {
    ESP_LOGE(TAG, "GT911 not responding at 0x%02X", config_with_addr.dev_addr);
    return NULL;
  }

  // Use Shared Bus Handle!
  ret = esp_lcd_new_panel_io_i2c(bus_handle, &config_with_addr, &tp_io_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create GT911 panel IO at 0x%02X: %s",
             config_with_addr.dev_addr, esp_err_to_name(ret));
    return NULL;
  }

  // 4. Initialize Touch Driver
  esp_lcd_touch_io_gt911_config_t gt911_io_cfg = {
      .dev_addr = config_with_addr.dev_addr,
  };

  esp_lcd_touch_config_t tp_cfg = {
      .x_max = CONFIG_ARS_TOUCH_X_MAX,
      .y_max = CONFIG_ARS_TOUCH_Y_MAX,
      .rst_gpio_num = GPIO_NUM_NC, // Reset already handled by BSP
      .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
      .levels = {.reset = 0, .interrupt = 0},
      .flags =
          {
              .swap_xy = CONFIG_ARS_TOUCH_SWAP_XY,
              .mirror_x = CONFIG_ARS_TOUCH_MIRROR_X,
              .mirror_y = CONFIG_ARS_TOUCH_MIRROR_Y,
          },
      .driver_data = &gt911_io_cfg,
  };

  ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Initialization at 0x%02X failed, retrying 0x14...",
             current_addr);

    // Clean up previous IO
    esp_lcd_panel_io_del(tp_io_handle);

    // Retry Reset for 0x14 (INT High)
    gpio_set_direction(int_pin, GPIO_MODE_OUTPUT);
    // Retry Reset for 0x5D (INT High -> Low? Logic inverted for 0x5D)
    // To select 0x5D, we need INT=1 (based on previous logic check)
    // Wait, previous code said 1 = 0x5D.
    gpio_set_direction(int_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(int_pin, 1);

    IO_EXTENSION_Output(rst_pin_io, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    IO_EXTENSION_Output(rst_pin_io, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    gpio_set_direction(int_pin, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Retry Init at 0x5D (Fallback)
    config_with_addr.dev_addr = 0x5D; // Fallback to 0x5D
    sanitize_ret =
        DEV_I2C_SanitizeAddr(config_with_addr.dev_addr, &sanitized_addr);
    if (sanitize_ret != ESP_OK) {
      ESP_LOGE(TAG, "Invalid GT911 fallback address 0x%02X: %s",
               config_with_addr.dev_addr, esp_err_to_name(sanitize_ret));
      return NULL;
    }
    config_with_addr.dev_addr = sanitized_addr;

    ret = esp_lcd_new_panel_io_i2c(bus_handle, &config_with_addr, &tp_io_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create GT911 panel IO fallback 0x%02X: %s",
               config_with_addr.dev_addr, esp_err_to_name(ret));
      return NULL;
    }

    gt911_io_cfg.dev_addr = config_with_addr.dev_addr;
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);
  }

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GT911 Initialization Failed even after retry.");
    return NULL;
  }

  ESP_LOGI(TAG, "GT911 Initialized successfully at address 0x%02X",
           config_with_addr.dev_addr);

  // FIX: Perform safe config update with checksum
  // This ensures Resolution, Sensitivity, and Checksum are correct.
  esp_err_t res_ret = gt911_update_config(tp_handle);
  if (res_ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to update GT911 config (non-fatal)");
  } else {
    ESP_LOGI(TAG, "GT911 Config Verification / Update Complete");
  }

  // Dump GT911 config for debugging
  gt911_dump_config();

  return tp_handle;
}

touch_gt911_point_t touch_gt911_read_point(uint8_t max_touch_cnt) {
  touch_gt911_point_t data;
  esp_lcd_touch_read_data(tp_handle);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  esp_lcd_touch_get_coordinates(tp_handle, data.x, data.y, NULL, &data.cnt,
                                max_touch_cnt);
#pragma GCC diagnostic pop
  return data;
}

/*******************************************************************************
 * Private API function
 *******************************************************************************/

/* Reset controller */
static esp_err_t touch_gt911_reset(esp_lcd_touch_handle_t tp) {
  assert(tp != NULL);

  gpio_num_t int_gpio = tp->config.int_gpio_num;
  bool int_configured = int_gpio != GPIO_NUM_NC;
  esp_err_t ret = ESP_OK;

  if (int_configured) {
    gpio_intr_disable(int_gpio);
    const gpio_config_t int_out_cfg = {
        .pin_bit_mask = BIT64(int_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&int_out_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "INT reconfig (output-low) failed: %s",
               esp_err_to_name(ret));
      goto restore_int;
    }

    ret = gpio_set_level(int_gpio, 0);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to drive INT low: %s", esp_err_to_name(ret));
      goto restore_int;
    }
  }

  if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
    ret = gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "GPIO reset low failed: %s", esp_err_to_name(ret));
      goto restore_int;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    ret =
        gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "GPIO reset high failed: %s", esp_err_to_name(ret));
      goto restore_int;
    }
    vTaskDelay(pdMS_TO_TICKS(60));
  } else {
    ret = gt911_reset_via_ioext();
    if (ret == ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGW(TAG, "GT911 reset requested but no reset line available");
    } else if (ret != ESP_OK) {
      goto restore_int;
    }
  }

restore_int:
  if (int_configured) {
    const gpio_config_t int_in_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = BIT64(int_gpio)};
    esp_err_t cfg_ret = gpio_config(&int_in_cfg);
    if (cfg_ret != ESP_OK) {
      ESP_LOGE(TAG, "INT restore failed: %s", esp_err_to_name(cfg_ret));
      if (ret == ESP_OK) {
        ret = cfg_ret;
      }
    } else {
      gpio_intr_enable(int_gpio);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }

  return ret;
}

static esp_err_t touch_gt911_read_cfg(esp_lcd_touch_handle_t tp) {
  uint8_t buf[4];

  assert(tp != NULL);

  ESP_RETURN_ON_ERROR(touch_gt911_i2c_read(tp,
                                           ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG,
                                           (uint8_t *)&buf[0], 3),
                      TAG, "GT911 read error!");
  ESP_RETURN_ON_ERROR(touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_CONFIG_REG,
                                           (uint8_t *)&buf[3], 1),
                      TAG, "GT911 read error!");

  ESP_LOGI(TAG, "TouchPad_ID:0x%02x,0x%02x,0x%02x", buf[0], buf[1], buf[2]);
  ESP_LOGI(TAG, "TouchPad_Config_Version:%d", buf[3]);

  return ESP_OK;
}

// GT911 additional config registers for diagnostic
#define GT911_REG_TOUCH_NUMBER (0x804C)     // Max touch points (default 5)
#define GT911_REG_MODULE_SWITCH1 (0x804D)   // Module switch 1
#define GT911_REG_MODULE_SWITCH2 (0x804E)   // Module switch 2
#define GT911_REG_SHAKE_CNT (0x804F)        // Shake count (debounce)
#define GT911_REG_FILTER (0x8050)           // Filter
#define GT911_REG_LARGE_TOUCH (0x8051)      // Large touch
#define GT911_REG_NOISE_REDUCTION (0x8052)  // Noise reduction
#define GT911_REG_SCREEN_TOUCH_LVL (0x8053) // Screen touch level
#define GT911_REG_SCREEN_LEAVE_LVL (0x8054) // Screen leave level
#define GT911_REG_REFRESH_RATE (0x8056)     // Refresh rate (5+N ms)

esp_err_t gt911_dump_config(void) {
  if (!tp_handle) {
    ESP_LOGE(TAG, "GT911 not initialized, cannot dump config");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_config_dumped_once) {
    ESP_LOGD(TAG, "GT911 config dump already emitted");
    return ESP_OK;
  }
  s_config_dumped_once = true;

  ESP_LOGW(TAG, "========== GT911 CONFIG DUMP ==========");

  // Product ID (0x8140-0x8143)
  uint8_t product_id[4] = {0};
  esp_err_t ret = touch_gt911_i2c_read(
      tp_handle, ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG, product_id, 4);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Product ID: %c%c%c%c (0x%02X%02X%02X%02X)", product_id[0],
             product_id[1], product_id[2], product_id[3], product_id[0],
             product_id[1], product_id[2], product_id[3]);
  } else {
    ESP_LOGE(TAG, "Failed to read product ID: %s", esp_err_to_name(ret));
  }

  // Firmware version (0x8144-0x8145)
  uint8_t fw_version[2] = {0};
  ret = touch_gt911_i2c_read(tp_handle, 0x8144, fw_version, 2);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Firmware Version: 0x%02X%02X", fw_version[1], fw_version[0]);
  }

  // Config version (0x8047)
  uint8_t config_version = 0;
  ret = touch_gt911_i2c_read(tp_handle, ESP_LCD_TOUCH_GT911_CONFIG_REG,
                             &config_version, 1);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Config Version: 0x%02X (%d)", config_version,
             config_version);
    if (config_version < 0x41) {
      ESP_LOGE(TAG, "  WARNING: Config version is LOW (expected >= 0x41)");
    }
  }

  // Resolution (0x8048-0x804B)
  uint8_t res_buf[4] = {0};
  ret = touch_gt911_i2c_read(tp_handle, GT911_REG_X_OUTPUT_MAX_L, res_buf, 4);
  if (ret == ESP_OK) {
    uint16_t x_max = (res_buf[1] << 8) | res_buf[0];
    uint16_t y_max = (res_buf[3] << 8) | res_buf[2];
    ESP_LOGW(TAG, "Resolution: X_MAX=%d, Y_MAX=%d", x_max, y_max);
    if (x_max == 0 || y_max == 0) {
      ESP_LOGE(TAG, "  WARNING: Resolution is ZERO - touch will not work!");
    }
    if (x_max != CONFIG_ARS_TOUCH_X_MAX || y_max != CONFIG_ARS_TOUCH_Y_MAX) {
      ESP_LOGW(TAG, "  MISMATCH: Expected X=%d, Y=%d", CONFIG_ARS_TOUCH_X_MAX,
               CONFIG_ARS_TOUCH_Y_MAX);
    }
  }

  // Touch number (0x804C)
  uint8_t touch_number = 0;
  ret =
      touch_gt911_i2c_read(tp_handle, GT911_REG_TOUCH_NUMBER, &touch_number, 1);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Max Touch Points: %d", touch_number);
    if (touch_number == 0) {
      ESP_LOGE(TAG, "  WARNING: Max touch is 0 - touch_cnt will always be 0!");
    }
  }

  // Module switch 1 (0x804D)
  uint8_t module_switch1 = 0;
  ret = touch_gt911_i2c_read(tp_handle, GT911_REG_MODULE_SWITCH1,
                             &module_switch1, 1);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Module Switch 1: 0x%02X (X2Y=%d, INT=%d)", module_switch1,
             (module_switch1 >> 3) & 1, // X2Y swap
             module_switch1 & 0x03);    // INT trigger mode
  }

  // Touch detection thresholds
  uint8_t touch_lvl = 0, leave_lvl = 0;
  touch_gt911_i2c_read(tp_handle, GT911_REG_SCREEN_TOUCH_LVL, &touch_lvl, 1);
  touch_gt911_i2c_read(tp_handle, GT911_REG_SCREEN_LEAVE_LVL, &leave_lvl, 1);
  ESP_LOGW(TAG, "Touch Level: %d, Leave Level: %d", touch_lvl, leave_lvl);
  if (touch_lvl == 0 && leave_lvl == 0) {
    ESP_LOGE(TAG, "  WARNING: Touch thresholds are 0!");
  }

  // Refresh rate (0x8056)
  uint8_t refresh = 0;
  ret = touch_gt911_i2c_read(tp_handle, GT911_REG_REFRESH_RATE, &refresh, 1);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "Refresh Rate: %d ms (5+%d)", 5 + refresh, refresh);
  }

  // Checksum (0x80FF) and Config Fresh (0x8100)
  uint8_t checksum = 0, config_fresh = 0;
  touch_gt911_i2c_read(tp_handle, GT911_REG_CONFIG_CHKSUM, &checksum, 1);
  touch_gt911_i2c_read(tp_handle, GT911_REG_CONFIG_FRESH, &config_fresh, 1);
  ESP_LOGW(TAG, "Checksum: 0x%02X, Config Fresh: 0x%02X", checksum,
           config_fresh);

  // Current status register (0x814E)
  uint8_t status = 0;
  ret = touch_gt911_i2c_read(tp_handle, ESP_LCD_TOUCH_GT911_STATUS_REG, &status,
                             1);
  if (ret == ESP_OK) {
    ESP_LOGW(TAG,
             "Status Reg: 0x%02X (buffer=%d, large=%d, prox=%d, havekey=%d, "
             "touch_cnt=%d)",
             status,
             (status >> 7) & 1, // Buffer status
             (status >> 6) & 1, // Large touch
             (status >> 5) & 1, // Proximity
             (status >> 4) & 1, // Have key
             status & 0x0F);    // Touch count
  }

  ESP_LOGW(TAG, "========================================");

  return ESP_OK;
}

/**
 * @brief Calculate GT911 config checksum
 * checksum = (~sum) + 1
 */
static uint8_t gt911_calc_checksum(uint8_t *config, int len) {
  uint8_t checksum = 0;
  for (int i = 0; i < len; i++) {
    checksum += config[i];
  }
  return (~checksum) + 1;
}

/**
 * @brief Update GT911 configuration safely (Read-Modify-Write)
 */
static esp_err_t gt911_update_config(esp_lcd_touch_handle_t tp) {
  esp_err_t ret = ESP_OK;
  uint8_t config[186]; // GT911 config size is usually ~184-186 bytes

  ESP_LOGI(TAG, "Checking GT911 Configuration...");

  // 1. Read existing config
  // Config register starts at 0x8047. We read enough bytes to cover the whole
  // block. 0x8047 to 0x80FE + checksum(0x80FF)
  ret = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_CONFIG_REG, config,
                             sizeof(config));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read GT911 config");
    return ret;
  }

  // 2. Modify Resolution (X_MAX @ 0x8048, Y_MAX @ 0x804A)
  // Indexes are relative to 0x8047, so:
  // 0x8048 - 0x8047 = 1 (X_Low)
  // 0x8049 - 0x8047 = 2 (X_High)
  // 0x804A - 0x8047 = 3 (Y_Low)
  // 0x804B - 0x8047 = 4 (Y_High)

  uint16_t x_max = CONFIG_ARS_TOUCH_X_MAX;
  uint16_t y_max = CONFIG_ARS_TOUCH_Y_MAX;

  uint16_t current_x = config[1] | (config[2] << 8);
  uint16_t current_y = config[3] | (config[4] << 8);

  bool changed = false;

  if (current_x != x_max || current_y != y_max) {
    ESP_LOGI(TAG, "Updating Resolution: (%d,%d) -> (%d,%d)", current_x,
             current_y, x_max, y_max);
    config[1] = x_max & 0xFF;
    config[2] = (x_max >> 8) & 0xFF;
    config[3] = y_max & 0xFF;
    config[4] = (y_max >> 8) & 0xFF;
    changed = true;
  }

  // 3. Ensure Touch Level (Sensitivity) is reasonable
  // 0x8053 - 0x8047 = 12 (0x0C) -> Touch Level
  // 0x8054 - 0x8047 = 13 (0x0D) -> Leave Level
  // Default recommendation: Touch=60-100, Leave=40-80
  if (config[12] == 0) {
    ESP_LOGW(TAG, "Fixing invalid Touch Level (0 -> 80)");
    config[12] = 80;
    changed = true;
  }
  if (config[13] == 0) {
    ESP_LOGW(TAG, "Fixing invalid Leave Level (0 -> 50)");
    config[13] = 50;
    changed = true;
  }

  if (!changed) {
    ESP_LOGI(TAG, "GT911 Configuration is already correct.");
    return ESP_OK;
  }

  // 4. Recalculate Checksum
  // Checksum is at 0x80FF. Relative index: 0x80FF - 0x8047 = 184 (0xB8)
  // Checksum range is 0x8047 to 0x80FE (184 bytes)
  uint8_t checksum = gt911_calc_checksum(config, 184);
  config[184] = checksum;
  config[185] = 1; // Config Fresh = 1

  ESP_LOGI(TAG, "Writing new config with Checksum 0x%02X...", checksum);

  // 5. Write back config
  // We write the whole block to be safe, or at least up to checksum + fresh
  ret = touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_CONFIG_REG, config[0]);
  // Wait... the write function writes a single byte. We need block write.
  // We need to loop or use a block write helper.
  // The existing touch_gt911_i2c_write only writes 1 byte.
  // We will iterate for now, but block write is better.
  // Actually, standard i2c_panel_io_tx_param can write buffer.

  bool locked = i2c_bus_shared_lock(pdMS_TO_TICKS(500));
  if (!locked)
    return ESP_ERR_TIMEOUT;

  ret = esp_lcd_panel_io_tx_param(tp->io, ESP_LCD_TOUCH_GT911_CONFIG_REG,
                                  config, 186);

  i2c_bus_shared_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write GT911 config: %s", esp_err_to_name(ret));
    return ret;
  }

  // Wait for config to apply
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "GT911 Configuration Updated Successfully.");
  return ESP_OK;
}

static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg,
                                      uint8_t *data, uint8_t len) {
  assert(tp != NULL);
  assert(data != NULL);

  /* Read data with retry */
  esp_err_t err = ESP_FAIL;

  bool locked = i2c_bus_shared_lock(pdMS_TO_TICKS(200));
  if (!locked) {
    return ESP_ERR_TIMEOUT;
  }

  for (int i = 0; i < 3; i++) {
    err = esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  i2c_bus_shared_unlock();
  return err;
}

static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg,
                                       uint8_t data) {
  assert(tp != NULL);

  // *INDENT-OFF*
  /* Write data with retry */
  esp_err_t err = ESP_FAIL;

  bool locked = i2c_bus_shared_lock(pdMS_TO_TICKS(200));
  if (!locked) {
    return ESP_ERR_TIMEOUT;
  }

  for (int i = 0; i < 3; i++) {
    err = esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  i2c_bus_shared_unlock();
  return err;
  // *INDENT-ON*
}

static void IRAM_ATTR gt911_gpio_isr_handler(void *arg) {
  BaseType_t hp_task_woken = pdFALSE;
  esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)arg;
  if (!tp) {
    return;
  }

  gpio_intr_disable(tp->config.int_gpio_num);
  s_gt911_irq_total++;
  if (!s_gt911_irq_task) {
    gpio_intr_enable(tp->config.int_gpio_num);
    return;
  }

  vTaskNotifyGiveFromISR(s_gt911_irq_task, &hp_task_woken);
  if (hp_task_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static void gt911_irq_task(void *arg) {
  esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)arg;
  const int64_t debounce_us = 5000; // 5 ms anti-bounce
  uint32_t error_backoff_ms = 10;

  while (1) {
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_poll_interval_ms));
    int64_t now = esp_timer_get_time();

    if (s_spurious_block_until_us > now && !s_poll_mode) {
      // Ignore rapid spurious IRQs for a short cooldown window
      gt911_enable_irq_guarded();
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    if (s_poll_mode) {
      esp_err_t err = esp_lcd_touch_gt911_read_data(tp);
      if (err != ESP_OK) {
        gt911_mark_i2c_error();
        vTaskDelay(pdMS_TO_TICKS(error_backoff_ms));
        if (error_backoff_ms < 200) {
          error_backoff_ms = (error_backoff_ms < 50) ? 50 : 200;
        }
      } else {
        error_backoff_ms = 10;
      }

      if (now >= s_poll_mode_until_us) {
        s_poll_mode = false;
        s_empty_window_count = 0;
        s_empty_window_start_us = 0;
        s_poll_interval_ms = GT911_POLL_INTERVAL_MS;
        gt911_enable_irq_guarded();
      } else {
        vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
      }
      continue;
    }

    if (notified == 0) {
      // Fallback: If no IRQ is received during the wait window, poll once to
      // avoid losing touches when the interrupt line is noisy or missing.
      gt911_mark_poll_timeout();
      esp_err_t err = esp_lcd_touch_gt911_read_data(tp);
      if (err != ESP_OK) {
        gt911_mark_i2c_error();
        vTaskDelay(pdMS_TO_TICKS(error_backoff_ms));
        if (error_backoff_ms < 200) {
          error_backoff_ms = (error_backoff_ms < 50) ? 50 : 200;
        }
      } else {
        error_backoff_ms = 10;
      }
      continue;
    }

    if ((now - s_last_irq_us) < debounce_us) {
      gt911_enable_irq_guarded();
      continue;
    }
    s_last_irq_us = now;

    // FIX: Add delay after IRQ to allow GT911 to populate registers
    vTaskDelay(pdMS_TO_TICKS(GT911_POST_IRQ_DELAY_MS));

    esp_err_t err = esp_lcd_touch_gt911_read_data(tp);
    if (err != ESP_OK) {
      gt911_mark_i2c_error();
      vTaskDelay(pdMS_TO_TICKS(error_backoff_ms));
      if (error_backoff_ms < 200) {
        error_backoff_ms = (error_backoff_ms < 50) ? 50 : 200;
      }
    } else {
      error_backoff_ms = 10;
      if (s_gt911_irq_storm > 0 && (s_gt911_irq_storm % 50) == 0) {
        ESP_LOGW(TAG, "IRQ storm detected (count=%" PRIu32 ")",
                 s_gt911_irq_storm);
      }
    }

    gt911_enable_irq_guarded();
  }
}
