#include "i2c_bus_shared.h"
#include "board.h" // Ensure ARS_I2C definitions are visible
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c.h"
#include <assert.h>

static const char *TAG = "i2c_bus_shared";

// Recursive mutex to allow recovery within lock
SemaphoreHandle_t g_i2c_bus_mutex = NULL;
static i2c_master_bus_handle_t s_shared_bus = NULL;
static uint32_t s_consecutive_recover_fails = 0;
static bool s_initialized = false;

// Forward declaration
static esp_err_t i2c_bus_shared_recover_internal(void);

// Helper: Check if current task holds the recursive mutex
bool i2c_bus_shared_is_locked_by_me(void) {
  if (g_i2c_bus_mutex == NULL)
    return false;
  return xSemaphoreGetMutexHolder(g_i2c_bus_mutex) ==
         xTaskGetCurrentTaskHandle();
}

esp_err_t i2c_bus_shared_init(void) {
  if (s_initialized && s_shared_bus != NULL) {
    return ESP_OK;
  }

  // Create recursive mutex
  if (g_i2c_bus_mutex == NULL) {
    g_i2c_bus_mutex = xSemaphoreCreateRecursiveMutex();
    if (!g_i2c_bus_mutex) {
      ESP_LOGE(TAG, "Failed to create I2C mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  // Check for stuck bus at boot
  gpio_set_direction(ARS_I2C_SDA, GPIO_MODE_INPUT);
  gpio_set_direction(ARS_I2C_SCL, GPIO_MODE_INPUT);
  int sda_level = gpio_get_level(ARS_I2C_SDA);
  int scl_level = gpio_get_level(ARS_I2C_SCL);

  if (sda_level == 0 || scl_level == 0) {
    ESP_LOGW(TAG, "I2C bus stuck at boot (SDA=%d SCL=%d), recovering...",
             sda_level, scl_level);

    gpio_config_t recover_cfg = {
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = true,
        .pull_down_en = false,
        .pin_bit_mask = (1ULL << ARS_I2C_SCL) | (1ULL << ARS_I2C_SDA),
    };
    gpio_config(&recover_cfg);

    // Toggle SCL
    gpio_set_level(ARS_I2C_SDA, 1);
    for (int i = 0; i < 9; i++) {
      gpio_set_level(ARS_I2C_SCL, 0);
      esp_rom_delay_us(10);
      gpio_set_level(ARS_I2C_SCL, 1);
      esp_rom_delay_us(10);
    }
    // Stop
    gpio_set_level(ARS_I2C_SDA, 0);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SCL, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SDA, 1);
  }

  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = ARS_I2C_PORT,
      .scl_io_num = ARS_I2C_SCL,
      .sda_io_num = ARS_I2C_SDA,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
      .intr_priority = 0,
  };

  ESP_LOGI(TAG, "Init shared I2C bus 0 (SCL=%d SDA=%d)", ARS_I2C_SCL,
           ARS_I2C_SDA);

  esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &s_shared_bus);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init shared I2C bus: %s", esp_err_to_name(ret));
  } else {
    s_initialized = true;
  }

  return ret;
}

bool i2c_bus_shared_lock(TickType_t timeout_ticks) {
  if (xPortInIsrContext()) {
    return false;
  }

  if (g_i2c_bus_mutex == NULL) {
    if (i2c_bus_shared_init() != ESP_OK) {
      return false;
    }
  }

  return xSemaphoreTakeRecursive(g_i2c_bus_mutex, timeout_ticks) == pdTRUE;
}

void i2c_bus_shared_unlock(void) {
  if (g_i2c_bus_mutex) {
    xSemaphoreGiveRecursive(g_i2c_bus_mutex);
  }
}

i2c_master_bus_handle_t i2c_bus_shared_get_handle(void) { return s_shared_bus; }

bool i2c_bus_shared_is_ready(void) {
  return s_shared_bus != NULL && s_initialized;
}

static esp_err_t i2c_bus_shared_recover_internal(void) {
  if (s_shared_bus) {
    i2c_del_master_bus(s_shared_bus);
    s_shared_bus = NULL;
  }

  // Manual bit-bang recovery
  gpio_config_t cfg = {
      .mode = GPIO_MODE_OUTPUT_OD,
      .pull_up_en = true,
      .pull_down_en = false,
      .pin_bit_mask = (1ULL << ARS_I2C_SCL) | (1ULL << ARS_I2C_SDA),
  };
  gpio_config(&cfg);

  gpio_set_level(ARS_I2C_SDA, 1);
  for (int i = 0; i < 9; i++) {
    gpio_set_level(ARS_I2C_SCL, 0);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SCL, 1);
    esp_rom_delay_us(10);
  }
  gpio_set_level(ARS_I2C_SDA, 0);
  esp_rom_delay_us(10);
  gpio_set_level(ARS_I2C_SCL, 1);
  esp_rom_delay_us(10);
  gpio_set_level(ARS_I2C_SDA, 1);

  vTaskDelay(pdMS_TO_TICKS(10));

  s_initialized = false;
  return i2c_bus_shared_init();
}

esp_err_t i2c_bus_shared_recover(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (i2c_bus_shared_is_locked_by_me()) {
    return i2c_bus_shared_recover_internal();
  }

  if (!i2c_bus_shared_lock(pdMS_TO_TICKS(1000))) {
    s_consecutive_recover_fails++;
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = i2c_bus_shared_recover_internal();

  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_recover_locked(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  // Caller MUST already hold the mutex
  if (!i2c_bus_shared_is_locked_by_me()) {
    ESP_LOGE(TAG,
             "i2c_bus_shared_recover_locked called without holding mutex!");
    return ESP_ERR_INVALID_STATE;
  }

  return i2c_bus_shared_recover_internal();
}

void i2c_bus_shared_deinit(void) {
  if (s_shared_bus) {
    i2c_del_master_bus(s_shared_bus);
    s_shared_bus = NULL;
  }
  if (g_i2c_bus_mutex) {
    vSemaphoreDelete(g_i2c_bus_mutex);
    g_i2c_bus_mutex = NULL;
  }
  s_initialized = false;
}
