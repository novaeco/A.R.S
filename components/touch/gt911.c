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
#include <stdio.h>
#include <string.h>

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
#include "io_extension.h"
#include "rgb_lcd_port.h"

#include "gt911.h"

static const char *TAG = "GT911";

/* GT911 registers */
#define ESP_LCD_TOUCH_GT911_READ_KEY_REG (0x8093)
#define ESP_LCD_TOUCH_GT911_READ_XY_REG (0x814E)
#define ESP_LCD_TOUCH_GT911_CONFIG_REG (0x8047)
#define ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG (0x8140)
#define ESP_LCD_TOUCH_GT911_ENTER_SLEEP (0x8040)

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

/* I2C read/write */
static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg,
                                      uint8_t *data, uint8_t len);
static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg,
                                       uint8_t data);

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
    // Info log instead of warning, as this is the expected path for this board
    // where RST is handled by BSP/IO Expander earlier.
    ESP_LOGI(TAG,
             "Address init skipped (no rst/int GPIO), using configured address "
             "0x%02X",
             gt911_config ? gt911_config->dev_addr : 0);
    /* Reset controller */
    ret = touch_gt911_reset(esp_lcd_touch_gt911);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GT911 reset failed");
  }

  /* Prepare pin for touch interrupt */
  if (esp_lcd_touch_gt911->config.int_gpio_num != GPIO_NUM_NC) {
    const gpio_config_t int_gpio_config = {
        .mode = GPIO_MODE_INPUT,
        .intr_type =
            (esp_lcd_touch_gt911->config.levels.interrupt ? GPIO_INTR_POSEDGE
                                                          : GPIO_INTR_NEGEDGE),
        .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num)};
    ret = gpio_config(&int_gpio_config);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

    /* Register interrupt callback */
    if (esp_lcd_touch_gt911->config.interrupt_callback) {
      esp_lcd_touch_register_interrupt_callback(
          esp_lcd_touch_gt911, esp_lcd_touch_gt911->config.interrupt_callback);
    }
  }

  /* Read status and config info */
  ret = touch_gt911_read_cfg(esp_lcd_touch_gt911);
  ESP_GOTO_ON_ERROR(ret, err, TAG, "GT911 init failed");

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

static esp_err_t esp_lcd_touch_gt911_read_data(esp_lcd_touch_handle_t tp) {
  esp_err_t err;
  uint8_t buf[41];
  uint8_t touch_cnt = 0;
  uint8_t clear = 0;
  size_t i = 0;
  static int s_consecutive_errors = 0; // ARS: Error counter

  assert(tp != NULL);

  // ARS: Self-Healing removed (IO handle assumed persistent)
  // if (gt911_ensure_io_handle(tp) != ESP_OK) { return ESP_FAIL; }

  err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, buf, 1);
  if (err != ESP_OK) {
    s_consecutive_errors++;
    ESP_LOGW(TAG, "I2C read error (stats: %d)", s_consecutive_errors);

    // ARS: Reset Threshold
    if (s_consecutive_errors > 5) {
      ESP_LOGE(TAG, "Too many I2C errors (%d), resetting GT911...",
               s_consecutive_errors);
      touch_gt911_reset(tp); // Pulse RST
      // Re-init or re-send config could be added here if needed
      s_consecutive_errors = 0;
    }
    return err;
  }

  // Success, reset error counter
  if (s_consecutive_errors > 0) {
    ESP_LOGI(TAG, "I2C recovered after %d errors", s_consecutive_errors);
    s_consecutive_errors = 0;
  }

  /* Any touch data? */
  if ((buf[0] & 0x80) == 0x00) {
    // Data not ready, do not clear!
    return ESP_OK;
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
  } else if ((buf[0] & 0x10) == 0x10) {
    /* Read all keys */
    uint8_t key_max = ((ESP_GT911_TOUCH_MAX_BUTTONS < ESP_LCD_TOUCH_MAX_BUTTONS)
                           ? (ESP_GT911_TOUCH_MAX_BUTTONS)
                           : (ESP_LCD_TOUCH_MAX_BUTTONS));
    err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_KEY_REG, &buf[0],
                               key_max);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "I2C read keys error: %s", esp_err_to_name(err));
      return err;
    }

    /* Clear all */
    touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
    // Continue even if write failed to avoid locking

    portENTER_CRITICAL(&tp->data.lock);

    /* Buttons count */
    tp->data.buttons = key_max;
    for (i = 0; i < key_max; i++) {
      tp->data.button[i].status = buf[0] ? 1 : 0;
    }

    portEXIT_CRITICAL(&tp->data.lock);
#endif
  } else if ((buf[0] & 0x80) == 0x80) {
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    portENTER_CRITICAL(&tp->data.lock);
    for (i = 0; i < ESP_LCD_TOUCH_MAX_BUTTONS; i++) {
      tp->data.button[i].status = 0;
    }
    portEXIT_CRITICAL(&tp->data.lock);
#endif
    /* Count of touched points */
    touch_cnt = buf[0] & 0x0f;
    if (touch_cnt > 5 || touch_cnt == 0) {
      touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
      return ESP_OK;
    }

    /* Read all points */
    err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG + 1, &buf[1],
                               touch_cnt * 8);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "I2C read points error: %s", esp_err_to_name(err));
      return err;
    }

    /* Clear all */
    err = touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
    // Proceed even if clear failed

    portENTER_CRITICAL(&tp->data.lock);

    /* Number of touched points */
    touch_cnt = (touch_cnt > ESP_LCD_TOUCH_MAX_POINTS ? ESP_LCD_TOUCH_MAX_POINTS
                                                      : touch_cnt);
    tp->data.points = touch_cnt;

    /* Fill all coordinates */
    for (i = 0; i < touch_cnt; i++) {
      tp->data.coords[i].x =
          ((uint16_t)buf[(i * 8) + 3] << 8) + buf[(i * 8) + 2];
      tp->data.coords[i].y =
          (((uint16_t)buf[(i * 8) + 5] << 8) + buf[(i * 8) + 4]);
      tp->data.coords[i].strength =
          (((uint16_t)buf[(i * 8) + 7] << 8) + buf[(i * 8) + 6]);
    }

    portEXIT_CRITICAL(&tp->data.lock);
  }

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

// Cleanup and Self-Healing removed as the I2C bus is now persistent
// (Singleton/Shared) and recovery is non-destructive.

// Function to initialize the GT911 touch controller
esp_lcd_touch_handle_t touch_gt911_init() {
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  const esp_lcd_panel_io_i2c_config_t tp_io_config =
      ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

  // Initialize Shared I2C Bus First
  i2c_master_bus_handle_t bus_handle = NULL;
  esp_err_t ret = DEV_I2C_Init_Bus(&bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C Bus Init Failed");
    return NULL;
  }

  // Ensure IO Extension is ready via safe init/handle
  // Ensure IO Extension is ready via safe init
  IO_EXTENSION_Init();

  // 2. Perform Robust Reset Sequence (Waveshare specific)
  const int int_pin = EXAMPLE_PIN_NUM_TOUCH_INT;
  // USE ENUM HERE
  const int rst_pin_io = IO_EXTENSION_IO_1;

  ESP_LOGI(TAG, "Starting GT911 Reset Sequence...");

  // Step A: Set INT Output Low (Select 0x5D)
  gpio_set_direction(int_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(int_pin, 0);

  // Step B: Set RST Low (IO Expander)
  // Step B: Set RST Low (IO Expander)
  IO_EXTENSION_Output(rst_pin_io, 0);
  vTaskDelay(pdMS_TO_TICKS(20));

  // Step C: Set RST High
  // Step C: Set RST High
  IO_EXTENSION_Output(rst_pin_io, 1);
  vTaskDelay(pdMS_TO_TICKS(60)); // Wait >50ms

  // Step D: Set INT Input (End of address selection)
  gpio_set_direction(int_pin, GPIO_MODE_INPUT);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 3. I2C Address Scan / Validation
  uint8_t current_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
  // Sanitize Address immediately
  current_addr = DEV_I2C_SanitizeAddr(current_addr);

  ESP_LOGI(TAG, "Initialize I2C panel IO at address 0x%02X", current_addr);
  esp_lcd_panel_io_i2c_config_t config_with_addr = tp_io_config;
  config_with_addr.dev_addr = current_addr;

  // Use Shared Bus Handle!
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_i2c(bus_handle, &config_with_addr, &tp_io_handle));

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
    config_with_addr.dev_addr = DEV_I2C_SanitizeAddr(config_with_addr.dev_addr);

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i2c(bus_handle, &config_with_addr, &tp_io_handle));

    gt911_io_cfg.dev_addr = config_with_addr.dev_addr;
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);
  }

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GT911 Initialization Failed even after retry.");
    return NULL;
  }

  ESP_LOGI(TAG, "GT911 Initialized successfully at address 0x%02X",
           config_with_addr.dev_addr);
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

  if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
    ESP_RETURN_ON_ERROR(
        gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG,
        "GPIO set level error!");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(
        gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG,
        "GPIO set level error!");
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return ESP_OK;
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

static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg,
                                      uint8_t *data, uint8_t len) {
  assert(tp != NULL);
  assert(data != NULL);

  /* Read data with retry */
  esp_err_t err = ESP_FAIL;

  // TAKE MUTEX for the whole transaction
  if (xSemaphoreTake(g_i2c_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  for (int i = 0; i < 3; i++) {
    err = esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  xSemaphoreGive(g_i2c_bus_mutex);
  return err;
}

static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg,
                                       uint8_t data) {
  assert(tp != NULL);

  // *INDENT-OFF*
  /* Write data with retry */
  esp_err_t err = ESP_FAIL;

  // TAKE MUTEX for the whole transaction
  if (xSemaphoreTake(g_i2c_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  for (int i = 0; i < 3; i++) {
    err = esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  xSemaphoreGive(g_i2c_bus_mutex);
  return err;
  // *INDENT-ON*
}
