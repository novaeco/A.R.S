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
 ******************************************************************************/
#include "io_extension.h" // Include IO_EXTENSION driver header for GPIO functions

io_extension_obj_t IO_EXTENSION; // Define the global IO_EXTENSION object

/**
 * @brief Set the IO mode for the specified pins.
 *
 * This function sets the specified pins to input or output mode by writing to
 * the mode register.
 *
 * @param pin An 8-bit value where each bit represents a pin (0 = input, 1 =
 * output).
 */
#include "esp_log.h"
#include <rom/ets_sys.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_bus_shared.h"

static const char *TAG = "io_ext";
static bool s_ioext_initialized = false;
static SemaphoreHandle_t s_ioext_mutex = NULL;

static bool ioext_lock(void) {
  if (!s_ioext_mutex) {
    s_ioext_mutex = xSemaphoreCreateMutex();
  }
  if (!s_ioext_mutex)
    return false;
  return xSemaphoreTake(s_ioext_mutex, pdMS_TO_TICKS(200)) == pdTRUE;
}

static void ioext_unlock(void) {
  if (s_ioext_mutex)
    xSemaphoreGive(s_ioext_mutex);
}

/**
 * @brief Set the IO mode for the specified pins.
 *
 * This function sets the specified pins to input or output mode by writing to
 * the mode register.
 *
 * @param pin An 8-bit value where each bit represents a pin (0 = input, 1 =
 * output).
 */
void IO_EXTENSION_IO_Mode(uint8_t pin) {
  uint8_t data[2] = {IO_EXTENSION_Mode,
                     pin}; // Prepare the data to write to the mode register
  // Write the 8-bit value to the IO mode register
  esp_err_t ret = ESP_OK;
  if (!ioext_lock()) {
    ESP_LOGE(TAG, "IO_Mode Failed: mutex unavailable");
    return;
  }

  ret = DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);
  ioext_unlock();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IO_Mode Failed: %s", esp_err_to_name(ret));
  }
}

/**
 * @brief Initialize the IO_EXTENSION device.
 *
 * This function configures the slave addresses for different registers of the
 * IO_EXTENSION chip via I2C, and sets the control flags for input/output modes.
 */
esp_err_t IO_EXTENSION_Init() {
  if (s_ioext_initialized) {
    return ESP_OK;
  }

  i2c_bus_shared_init();
  if (!i2c_bus_shared_is_ready()) {
    ESP_LOGE(TAG, "IOEXT init aborted: shared I2C bus not ready");
    return ESP_ERR_INVALID_STATE;
  }

  // Set the I2C slave address for the IO_EXTENSION device
  esp_err_t ret = DEV_I2C_Set_Slave_Addr(&IO_EXTENSION.addr, IO_EXTENSION_ADDR);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT Set_Slave_Addr failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // PROBE: Try to write Mode register to check if device ACK
  uint8_t data[2] = {IO_EXTENSION_Mode, 0xff}; // Set all to output
  ret = ESP_OK;
  if (!ioext_lock()) {
    ESP_LOGE(TAG, "IOEXT PROBE FAIL: mutex unavailable");
    return ESP_ERR_TIMEOUT;
  }

  ret = DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);
  ioext_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IOEXT PROBE FAIL addr=0x%02X err=%s", IO_EXTENSION_ADDR,
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "IOEXT PROBE OK addr=0x%02X", IO_EXTENSION_ADDR);

  // Initialize control flags for IO output enable and open-drain output mode
  IO_EXTENSION.Last_io_value =
      0xFF; // All pins are initially set to high (output mode)
  IO_EXTENSION.Last_od_value =
      0xFF; // All pins are initially set to high (open-drain mode)
  s_ioext_initialized = true;
  return ESP_OK;
}

/**
 * @brief Set the value of the IO output pins on the IO_EXTENSION device.
 *
 * This function writes an 8-bit value to the IO output register. The value
 * determines the high or low state of the pins.
 *
 * @param pin The pin number to set (0-7).
 * @param value The value to set on the specified pin (0 = low, 1 = high).
 */
esp_err_t IO_EXTENSION_Output(uint8_t pin, uint8_t value) {
  return IO_EXTENSION_Output_With_Readback(pin, value, NULL, NULL);
}

esp_err_t IO_EXTENSION_Output_With_Readback(uint8_t pin, uint8_t value,
                                            uint8_t *latched_level,
                                            uint8_t *input_level) {
  if (!ioext_lock()) {
    ESP_LOGE(TAG, "Output Failed: mutex unavailable");
    return ESP_ERR_TIMEOUT;
  }

  // Serialize I2C access for the whole write+readback window to avoid collisions
  // with other users (e.g., touch) while SD is driving CS.
  bool bus_locked = DEV_I2C_TakeLock(pdMS_TO_TICKS(200));

  // Update the output value based on the pin and value
  if (value == 1)
    IO_EXTENSION.Last_io_value |= (1 << pin); // Set the pin high
  else
    IO_EXTENSION.Last_io_value &= (~(1 << pin)); // Set the pin low

  uint8_t data[2] = {IO_EXTENSION_IO_OUTPUT_ADDR, IO_EXTENSION.Last_io_value};
  esp_err_t ret = DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);

  if (ret == ESP_OK && latched_level) {
    // Allow the output latch to settle before re-reading
    ets_delay_us(120);
    uint8_t out_reg = 0;
    esp_err_t rb_ret =
        DEV_I2C_Read_Nbyte(IO_EXTENSION.addr, IO_EXTENSION_IO_OUTPUT_ADDR,
                           &out_reg, 1);
    if (rb_ret == ESP_OK) {
      *latched_level = (out_reg >> pin) & 0x1;
    } else {
      ESP_LOGW(TAG, "Output readback failed: %s", esp_err_to_name(rb_ret));
      ret = rb_ret;
    }
  }

  if (ret == ESP_OK && input_level) {
    uint8_t in_reg = 0;
    esp_err_t in_ret =
        DEV_I2C_Read_Nbyte(IO_EXTENSION.addr, IO_EXTENSION_IO_INPUT_ADDR,
                           &in_reg, 1);
    if (in_ret == ESP_OK) {
      *input_level = (in_reg >> pin) & 0x1;
    } else {
      ESP_LOGW(TAG, "Input sample failed: %s", esp_err_to_name(in_ret));
      ret = in_ret;
    }
  }

  if (bus_locked) {
    DEV_I2C_GiveLock();
  }

  ioext_unlock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Output Failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

esp_err_t IO_EXTENSION_Read_Output_Latch(uint8_t pin,
                                         uint8_t *latched_level) {
  if (!latched_level) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!ioext_lock()) {
    ESP_LOGE(TAG, "Read latch Failed: mutex unavailable");
    return ESP_ERR_TIMEOUT;
  }

  bool bus_locked = DEV_I2C_TakeLock(pdMS_TO_TICKS(200));

  uint8_t out_reg = 0;
  esp_err_t ret =
      DEV_I2C_Read_Nbyte(IO_EXTENSION.addr, IO_EXTENSION_IO_OUTPUT_ADDR,
                         &out_reg, 1);

  if (bus_locked) {
    DEV_I2C_GiveLock();
  }

  ioext_unlock();

  if (ret == ESP_OK) {
    *latched_level = (out_reg >> pin) & 0x1;
  } else {
    ESP_LOGE(TAG, "Read latch Failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

/**
 * @brief Read the value from the IO input pins on the IO_EXTENSION device.
 *
 * This function reads the value of the IO input register and returns the state
 * of the specified pins.
 *
 * @param pin The bit mask to specify which pin to read (e.g., 0x01 for the
 * first pin).
 * @return The value of the specified pin(s) (0 = low, 1 = high).
 */
uint8_t IO_EXTENSION_Input(uint8_t pin) {
  uint8_t value = 0;

  if (!ioext_lock()) {
    ESP_LOGE(TAG, "Input Failed: mutex unavailable");
    return 0;
  }

  // Read the value of the input pins
  DEV_I2C_Read_Nbyte(IO_EXTENSION.addr, IO_EXTENSION_IO_INPUT_ADDR, &value, 1);
  ioext_unlock();
  // Return the value of the specific pin(s) by masking with the provided bit
  // mask
  return ((value & (1 << pin)) > 0);
}

/**
 * @brief Set the PWM output value on the IO_EXTENSION device.
 *
 * This function sets the PWM output value, which controls the duty cycle of the
 * PWM signal. The duty cycle is calculated based on the input value and the
 * resolution (12 bits).
 *
 * @param Value The input value to set the PWM duty cycle (0-100).
 */
esp_err_t IO_EXTENSION_Pwm_Output(uint8_t Value) {
  // Prevent the screen from completely turning off
  if (Value >= 97) {
    Value = 97;
  }

  uint8_t data[2] = {IO_EXTENSION_PWM_ADDR,
                     Value}; // Prepare the data to write to the PWM register
  // Calculate the duty cycle based on the resolution (12 bits)
  data[1] = Value * (255 / 100.0);
  // Write the 8-bit value to the PWM output register
  if (!ioext_lock()) {
    ESP_LOGE(TAG, "PWM Failed: mutex unavailable");
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t ret = DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);
  ioext_unlock();
  return ret;
}

/**
 * @brief Read the ADC input value from the IO_EXTENSION device.
 *
 * This function reads the ADC input value from the IO_EXTENSION device.
 *
 * @return The ADC input value.
 */
uint16_t IO_EXTENSION_Adc_Input() {
  uint16_t value = 0;
  // Read the ADC input value from the IO_EXTENSION device
  DEV_I2C_Read_Word(IO_EXTENSION.addr, IO_EXTENSION_ADC_ADDR, &value);
  return value;
}

bool IO_EXTENSION_Is_Initialized(void) { return s_ioext_initialized; }

i2c_master_dev_handle_t IO_EXTENSION_Get_Handle(void) {
  return IO_EXTENSION.addr;
}