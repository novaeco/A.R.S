/*****************************************************************************
 * | File         :   io_extension.c
 * | Author       :   Waveshare team
 * | Function     :   IO_EXTENSION GPIO control via I2C interface
 * | Info         :
 * |                 I2C driver code for controlling GPIO pins using
 *IO_EXTENSION chip.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-27
 * | Info         :   Basic version, includes functions to read and write
 * |                 GPIO pins using I2C communication with IO_EXTENSION.
 *
 * P0-C Concurrency:
 * - All IOEXT operations use ars_i2c_lock() for I2C bus access
 * - Lock order to avoid deadlock:
 *   1. sd_extcs_lock() (if SD transaction)
 *   2. ars_i2c_lock() (always for I2C access)
 * - NEVER acquire locks in reverse order!
 * - CH32V003 does NOT support reliable I2C readback; use shadow state.
 *
 ******************************************************************************/
#include "io_extension.h" // Include IO_EXTENSION driver header for GPIO functions
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus_shared.h"
#include "freertos/semphr.h"
#include <rom/ets_sys.h>

io_extension_obj_t IO_EXTENSION; // Define the global IO_EXTENSION object

static const char *TAG = "io_ext";
static bool s_ioext_initialized = false;
static i2c_master_dev_handle_t s_ioext_handle = NULL;
static uint32_t s_ioext_error_streak = 0;
static int64_t s_ioext_next_recover_us = 0;
static int64_t s_ioext_last_busy_log_us = 0;
static SemaphoreHandle_t s_ioext_mutex = NULL;
static uint8_t s_ioext_out_shadow = 0xFF;

static bool ioext_take_bus(TickType_t wait_ticks, const char *ctx) {
  TickType_t backoff = i2c_bus_shared_backoff_ticks();
  if (backoff > 0) {
    vTaskDelay(backoff);
  }
  if (!ars_i2c_lock(pdTICKS_TO_MS(wait_ticks))) {
    int64_t now = esp_timer_get_time();
    if ((now - s_ioext_last_busy_log_us) > 200000) { // 200 ms
      ESP_LOGW(TAG, "%s: I2C bus busy", ctx);
      s_ioext_last_busy_log_us = now;
    }
    return false;
  }
  return true;
}

static void ioext_on_error(const char *ctx, esp_err_t err) {
  s_ioext_error_streak++;
  int64_t now = esp_timer_get_time();
  i2c_bus_shared_note_error(ctx, err);
  if (s_ioext_error_streak >= 3 && s_ioext_error_streak <= 6 &&
      now >= s_ioext_next_recover_us) {
    ESP_LOGW(TAG,
             "%s: attempting bus recovery after %u errors (%s) [ADDR=0x%02X "
             "SDA=8 SCL=9]",
             ctx, (unsigned)s_ioext_error_streak, esp_err_to_name(err),
             IO_EXTENSION_ADDR);
    i2c_bus_shared_recover();
    s_ioext_next_recover_us = now + 500000; // 500ms backoff
  }
  if (s_ioext_error_streak > 10) {
    ESP_LOGE(TAG, "%s: I2C bus unrecoverable after 10 errors", ctx);
  }
}

static void ioext_on_success(void) {
  s_ioext_error_streak = 0;
  i2c_bus_shared_note_success();
}

bool io_extension_lock(TickType_t timeout_ticks) {
  if (s_ioext_mutex == NULL) {
    s_ioext_mutex = xSemaphoreCreateRecursiveMutex();
  }
  if (s_ioext_mutex == NULL) {
    ESP_LOGE(TAG, "IOEXT mutex alloc failed");
    return false;
  }
  return xSemaphoreTakeRecursive(s_ioext_mutex, timeout_ticks) == pdTRUE;
}

void io_extension_unlock(void) {
  if (s_ioext_mutex) {
    xSemaphoreGiveRecursive(s_ioext_mutex);
  }
}

uint8_t io_extension_get_output_shadow(void) { return s_ioext_out_shadow; }

static esp_err_t io_extension_write_shadow_unsafe(void) {
  if (!s_ioext_initialized || s_ioext_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = ESP_OK;
  IO_EXTENSION.Last_io_value = s_ioext_out_shadow;
  uint8_t data[2] = {IO_EXTENSION_IO_OUTPUT_ADDR, s_ioext_out_shadow};

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "Output shadow")) {
    ESP_LOGW(TAG, "Output shadow skipped: bus busy");
    return ESP_ERR_TIMEOUT;
  }

  ret = i2c_master_transmit(s_ioext_handle, data, 2, pdMS_TO_TICKS(100));
  ars_i2c_unlock();
  return ret;
}

esp_err_t io_extension_write_shadow_locked(void) {
  return io_extension_write_shadow_unsafe();
}

esp_err_t io_extension_set_bits_locked(uint8_t mask) {
  s_ioext_out_shadow |= mask;
  return io_extension_write_shadow_locked();
}

esp_err_t io_extension_clear_bits_locked(uint8_t mask) {
  s_ioext_out_shadow &= (uint8_t)~mask;
  return io_extension_write_shadow_locked();
}

/**
 * @brief Set the IO mode for the specified pins.
 */
void IO_EXTENSION_IO_Mode(uint8_t pin) {
  esp_err_t ret = ESP_OK;
  uint8_t data[2] = {IO_EXTENSION_Mode, pin};

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "IO_Mode skipped: IOEXT mutex busy");
    return;
  }

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "IO_Mode")) {
    ESP_LOGW(TAG, "IO_Mode skipped: bus busy");
    io_extension_unlock();
    return;
  }

  ret = i2c_master_transmit(s_ioext_handle, data, 2, pdMS_TO_TICKS(100));
  ars_i2c_unlock();
  io_extension_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IO_Mode Failed: %s", esp_err_to_name(ret));
    ioext_on_error("IO_Mode", ret);
  } else {
    ioext_on_success();
  }
}

/**
 * @brief Initialize the IO_EXTENSION device.
 */
esp_err_t IO_EXTENSION_Init(void) {
  if (s_ioext_initialized && s_ioext_handle != NULL) {
    return ESP_OK;
  }

  if (s_ioext_mutex == NULL) {
    s_ioext_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_ioext_mutex == NULL) {
      ESP_LOGE(TAG, "IOEXT mutex alloc failed");
      return ESP_ERR_NO_MEM;
    }
  }

  esp_err_t ret = ESP_OK;
  esp_err_t bus_ret = i2c_bus_shared_init();
  if (bus_ret != ESP_OK) {
    ESP_LOGE(TAG, "Shared I2C init failed: %s", esp_err_to_name(bus_ret));
    return bus_ret;
  }

  i2c_master_bus_handle_t bus_handle = i2c_bus_shared_get_handle();
  if (!bus_handle) {
    ESP_LOGE(TAG, "Shared I2C bus handle is NULL");
    return ESP_ERR_INVALID_STATE;
  }

  // Add device to shared bus
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = IO_EXTENSION_ADDR,
      .scl_speed_hz = 400000,
  };

  if (s_ioext_handle == NULL) {
    esp_err_t add_ret =
        i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_ioext_handle);
    if (add_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to add IOEXT device: %s", esp_err_to_name(add_ret));
      return add_ret;
    }
  }

  // PROBE: Try to write Mode register to check if device ACK
  uint8_t data[2] = {IO_EXTENSION_Mode, 0xff}; // Set all to output

  for (int i = 0; i < 3; i++) {
    if (i > 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!ioext_take_bus(pdMS_TO_TICKS(200), "IOEXT PROBE")) {
      ESP_LOGW(TAG, "IOEXT PROBE skipped: bus busy");
      continue;
    }

    ret = i2c_master_transmit(s_ioext_handle, data, 2, pdMS_TO_TICKS(100));
    ars_i2c_unlock();

    if (ret == ESP_OK) {
      break;
    }

    ESP_LOGW(TAG, "IOEXT PROBE FAIL attempt %d addr=0x%02X err=%s", i + 1,
             IO_EXTENSION_ADDR, esp_err_to_name(ret));
    if (ret != ESP_OK) {
      ioext_on_error("IOEXT PROBE", ret);
      i2c_bus_shared_recover();
    }
  }

  if (ret != ESP_OK) {
    return ret;
  }
  ioext_on_success();
  ESP_LOGI(TAG, "IOEXT PROBE OK addr=0x%02X", IO_EXTENSION_ADDR);

  // Initialize control flags
  IO_EXTENSION.Last_io_value = 0xFF;
  IO_EXTENSION.Last_od_value = 0xFF;
  s_ioext_out_shadow = 0xFF;
  s_ioext_initialized = true;
  return ESP_OK;
}

/**
 * @brief Set the value of the IO output pins.
 */
esp_err_t IO_EXTENSION_Output(uint8_t pin, uint8_t value) {
  return IO_EXTENSION_Output_With_Readback(pin, value, NULL, NULL);
}

esp_err_t IO_EXTENSION_Output_With_Readback(uint8_t pin, uint8_t value,
                                            uint8_t *latched_level,
                                            uint8_t *input_level) {
  esp_err_t ret = ESP_OK;
  uint8_t out_reg = 0;
  uint8_t in_reg = 0;

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "Output skipped: IOEXT mutex busy");
    return ESP_ERR_TIMEOUT;
  }

  if (value == 1) {
    s_ioext_out_shadow |= (1 << pin);
  } else {
    s_ioext_out_shadow &= (uint8_t)~(1 << pin);
  }

  ret = io_extension_write_shadow_locked();
  if (ret != ESP_OK)
    goto cleanup;

  if (latched_level) {
    uint8_t reg_addr_out = IO_EXTENSION_IO_OUTPUT_ADDR;
    if (!ioext_take_bus(pdMS_TO_TICKS(200), "Output latch")) {
      ESP_LOGW(TAG, "Output latch skipped: bus busy");
      ret = ESP_ERR_TIMEOUT;
      goto cleanup;
    }
    // Allow the output latch to settle before re-reading
    ets_delay_us(120);
    ret = i2c_master_transmit_receive(s_ioext_handle, &reg_addr_out, 1,
                                      &out_reg, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
      *latched_level = (out_reg >> pin) & 0x1;
    } else {
      ESP_LOGW(TAG, "Output readback failed: %s", esp_err_to_name(ret));
    }
    ars_i2c_unlock();
  }

  if (ret == ESP_OK && input_level) {
    uint8_t reg_addr_in = IO_EXTENSION_IO_INPUT_ADDR;
    if (!ioext_take_bus(pdMS_TO_TICKS(200), "Input sample")) {
      ESP_LOGW(TAG, "Input sample skipped: bus busy");
      ret = ESP_ERR_TIMEOUT;
      goto cleanup;
    }
    ret = i2c_master_transmit_receive(s_ioext_handle, &reg_addr_in, 1, &in_reg,
                                      1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
      *input_level = (in_reg >> pin) & 0x1;
    } else {
      ESP_LOGW(TAG, "Input sample failed: %s", esp_err_to_name(ret));
    }
    ars_i2c_unlock();
  }

cleanup:
  io_extension_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Output Failed: %s", esp_err_to_name(ret));
    ioext_on_error("Output", ret);
  } else {
    ioext_on_success();
  }
  return ret;
}

esp_err_t IO_EXTENSION_Read_Output_Latch(uint8_t pin, uint8_t *latched_level) {
  esp_err_t ret = ESP_OK;
  uint8_t out_reg = 0;
  uint8_t reg_addr = IO_EXTENSION_IO_OUTPUT_ADDR;

  if (!latched_level) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "Read latch skipped: IOEXT mutex busy");
    return ESP_ERR_TIMEOUT;
  }

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "Read latch")) {
    ESP_LOGW(TAG, "Read latch skipped: bus busy");
    io_extension_unlock();
    return ESP_ERR_TIMEOUT;
  }

  ret = i2c_master_transmit_receive(s_ioext_handle, &reg_addr, 1, &out_reg, 1,
                                    pdMS_TO_TICKS(100));

  ars_i2c_unlock();
  io_extension_unlock();

  if (ret == ESP_OK) {
    *latched_level = (out_reg >> pin) & 0x1;
    ioext_on_success();
  } else {
    ESP_LOGE(TAG, "Read latch Failed: %s", esp_err_to_name(ret));
    ioext_on_error("Read latch", ret);
  }

  return ret;
}

/**
 * @brief Read the value from the IO input pins.
 */
uint8_t IO_EXTENSION_Input(uint8_t pin) {
  uint8_t value = 0;
  uint8_t reg_addr = IO_EXTENSION_IO_INPUT_ADDR;
  esp_err_t ret = ESP_OK;

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "Input skipped: IOEXT mutex busy");
    return 0;
  }

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "Input")) {
    ESP_LOGW(TAG, "Input skipped: bus busy");
    io_extension_unlock();
    return 0;
  }

  ret = i2c_master_transmit_receive(s_ioext_handle, &reg_addr, 1, &value, 1,
                                    pdMS_TO_TICKS(100));
  ars_i2c_unlock();
  io_extension_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Input Failed: %s", esp_err_to_name(ret));
    ioext_on_error("Input", ret);
    return 0;
  }
  ioext_on_success();

  return ((value & (1 << pin)) > 0);
}

/**
 * @brief Set the PWM output value.
 */
esp_err_t IO_EXTENSION_Pwm_Output(uint8_t Value) {
  esp_err_t ret = ESP_OK;

  // Prevent the screen from completely turning off
  if (Value >= 97) {
    Value = 97;
  }

  uint8_t data[2] = {IO_EXTENSION_PWM_ADDR, Value};
  // Calculate the duty cycle based on the resolution (12 bits)
  data[1] = Value * (255 / 100.0);

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "PWM skipped: IOEXT mutex busy");
    return ESP_ERR_TIMEOUT;
  }

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "PWM")) {
    ESP_LOGW(TAG, "PWM skipped: bus busy");
    io_extension_unlock();
    return ESP_ERR_TIMEOUT;
  }

  ret = i2c_master_transmit(s_ioext_handle, data, 2, pdMS_TO_TICKS(100));
  ars_i2c_unlock();
  io_extension_unlock();

  if (ret != ESP_OK) {
    ioext_on_error("PWM", ret);
  } else {
    ioext_on_success();
  }
  return ret;
}

/**
 * @brief Read the ADC input value.
 */
uint16_t IO_EXTENSION_Adc_Input(void) {
  uint16_t value = 0;
  uint8_t reg_addr = IO_EXTENSION_ADC_ADDR;
  uint8_t data[2] = {0};
  esp_err_t ret = ESP_OK;

  if (!io_extension_lock(pdMS_TO_TICKS(200))) {
    ESP_LOGW(TAG, "ADC read skipped: IOEXT mutex busy");
    return 0;
  }

  if (!ioext_take_bus(pdMS_TO_TICKS(200), "ADC")) {
    ESP_LOGW(TAG, "ADC read skipped: bus busy");
    io_extension_unlock();
    return 0;
  }

  ret = i2c_master_transmit_receive(s_ioext_handle, &reg_addr, 1, data, 2,
                                    pdMS_TO_TICKS(100));
  if (ret == ESP_OK) {
    value = (data[1] << 8) | data[0];
  }
  ars_i2c_unlock();
  io_extension_unlock();

  if (ret != ESP_OK) {
    ioext_on_error("ADC", ret);
    return 0;
  }
  ioext_on_success();
  return value;
}

bool IO_EXTENSION_Is_Initialized(void) { return s_ioext_initialized; }

i2c_master_dev_handle_t IO_EXTENSION_Get_Handle(void) { return s_ioext_handle; }
