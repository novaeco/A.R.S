/*****************************************************************************
 * | File      	 :   gpio.c
 * | Author      :   Waveshare team
 * | Function    :   Hardware underlying interface
 * | Info        :
 *                   GPIO driver code
 *----------------
 * |This version :   V1.0
 * | Date        :   2024-11-19
 * | Info        :   Basic version
 *
 ******************************************************************************/
#include "gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "gpio";
static bool s_isr_service_installed = false;

/**
 * @brief Configure a GPIO pin as input or output
 *
 * This function initializes a GPIO pin with the specified mode (input or
 * output). If set as input, it also enables the pull-up resistor by default.
 *
 * @param Pin GPIO pin number
 * @param Mode GPIO mode: 0 or GPIO_MODE_INPUT for input, others for output
 */
void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode) {
  // Zero-initialize the GPIO configuration structure
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupts for this pin
  io_conf.pin_bit_mask = 1ULL << Pin;    // Select the GPIO pin using a bitmask

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  if (Mode == 0 || Mode == GPIO_MODE_INPUT) {
    io_conf.mode = GPIO_MODE_INPUT;          // Set pin as input
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Enable internal pull-up resistor
  } else if (Mode == GPIO_MODE_INPUT_OUTPUT) {
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT;   // Bidirectional use-case
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Keep pull-up enabled for input
  } else {
    io_conf.mode = GPIO_MODE_OUTPUT;          // Set pin as output
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE; // Disable pull-up
  }

  gpio_config(&io_conf); // Apply the configuration
}

/**
 * @brief Configure a GPIO pin for interrupt handling
 *
 * This function sets up a GPIO pin to generate an interrupt on a negative edge
 * (falling edge) and registers the specified interrupt handler.
 *
 * @param Pin GPIO pin number
 * @param isr_handler Pointer to the interrupt handler function
 */
static esp_err_t ensure_isr_service(void) {
  if (s_isr_service_installed) {
    return ESP_OK;
  }

  esp_err_t ret = gpio_install_isr_service(0);
  if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "GPIO ISR service already installed");
    ret = ESP_OK;
  } else if (ret == ESP_OK) {
    s_isr_service_installed = true;
  } else {
    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
  }
  return ret;
}

void DEV_GPIO_INT(int32_t Pin, gpio_int_type_t intr_type, gpio_isr_t isr_handler) {
  // Zero-initialize the GPIO configuration structure
  gpio_config_t io_conf = {};
  io_conf.intr_type = intr_type; // Allow caller to choose edge
  io_conf.mode = GPIO_MODE_INPUT; // Set pin as input mode
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // Disable pull-down
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;      // Enable pull-up resistor
  io_conf.pin_bit_mask = 1ULL << Pin; // Select the GPIO pin using a bitmask

  gpio_config(&io_conf); // Apply the configuration

  // Install the GPIO interrupt service if not already installed
  if (ensure_isr_service() != ESP_OK) {
    return;
  }

  // Register the interrupt handler for the specified pin
  gpio_isr_handler_add(Pin, isr_handler, (void *)Pin);
}

/**
 * @brief Configure a GPIO pin for PWM output
 *
 * This function configures a GPIO pin to output PWM signals at the specified
 * frequency.
 *
 * @param Pin GPIO pin number
 * @param frequency PWM frequency in Hz
 */
void DEV_GPIO_PWM(uint16_t Pin, uint16_t frequency) {
  // Initialize and configure the LEDC PWM timer
  ledc_timer_config_t ledc_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE, // Use low-speed mode for the PWM timer
      .duty_resolution = LEDC_TIMER_12_BIT, // Set PWM resolution to 12 bits
      .timer_num = LEDC_TIMER_0,            // Use timer 0
      .freq_hz = frequency,                 // Set the PWM frequency
      .clk_cfg = LEDC_AUTO_CLK // Automatically select the clock source
  };
  esp_err_t err =
      ledc_timer_config(&ledc_timer); // Check and apply the timer configuration
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PWM timer config failed (pin=%u freq=%u): %s", Pin,
             (unsigned)frequency, esp_err_to_name(err));
    return;
  }

  // Initialize and configure the LEDC PWM channel
  ledc_channel_config_t ledc_channel = {
      .speed_mode =
          LEDC_LOW_SPEED_MODE,        // Use low-speed mode for the PWM channel
      .channel = LEDC_CHANNEL_0,      // Use channel 0
      .timer_sel = LEDC_TIMER_0,      // Link to timer 0
      .intr_type = LEDC_INTR_DISABLE, // Disable PWM interrupts
      .gpio_num = Pin,                // Assign the specified GPIO pin
      .duty = 0,                      // Set the initial duty cycle to 0%
      .hpoint = 0                     // Set the high point of the PWM signal
  };
  err = ledc_channel_config(
      &ledc_channel); // Check and apply the channel configuration
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PWM channel config failed (pin=%u): %s", Pin,
             esp_err_to_name(err));
  }
}

/**
 * @brief Set the PWM duty cycle
 *
 * This function sets the duty cycle for the PWM signal on the configured
 * channel.
 *
 * @param Value Duty cycle percentage (0 to 100)
 */
void DEV_SET_PWM(uint8_t Value) {
  if (Value > 100) {
    ESP_LOGW(TAG, "Please enter a value between 0 and 100");
  } else {
    // Calculate the duty cycle based on the resolution (12 bits)
    uint32_t duty = Value * ((1 << LEDC_TIMER_12_BIT) / 100.0);
    ESP_LOGI(TAG, "Duty cycle: %lu", (unsigned long)duty);

    // Set the new duty cycle for the PWM signal
    esp_err_t err =
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "PWM duty update failed: %s", esp_err_to_name(err));
      return;
    }
    // Apply the updated duty cycle
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "PWM duty latch failed: %s", esp_err_to_name(err));
    }
  }
}

/**
 * @brief Set the logic level of a GPIO pin
 *
 * This function sets the logic level (high or low) of a GPIO pin.
 *
 * @param Pin GPIO pin number
 * @param Value Logic level: 0 for low, 1 for high
 */
void DEV_Digital_Write(uint16_t Pin, uint8_t Value) {
  gpio_set_level(Pin, Value); // Set the GPIO pin level
}

/**
 * @brief Read the logic level of a GPIO pin
 *
 * This function reads and returns the current logic level of a GPIO pin.
 *
 * @param Pin GPIO pin number
 * @return uint8_t Logic level: 0 for low, 1 for high
 */
uint8_t DEV_Digital_Read(uint16_t Pin) {
  return gpio_get_level(Pin); // Get the GPIO pin level
}
