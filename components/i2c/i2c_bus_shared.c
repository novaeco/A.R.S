#include "i2c_bus_shared.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c.h" // Access DEV_I2C_Init_Bus
#include <assert.h>

static const char *TAG = "i2c_bus_shared";

// Define the global mutex
SemaphoreHandle_t g_i2c_bus_mutex = NULL;
static i2c_master_bus_handle_t s_shared_bus = NULL;

esp_err_t i2c_bus_shared_init(void) {
  static bool initialized = false;

  if (initialized && s_shared_bus != NULL) {
    return ESP_OK;
  }

  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (g_i2c_bus_mutex == NULL) {
    g_i2c_bus_mutex = xSemaphoreCreateRecursiveMutex();
    if (g_i2c_bus_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to allocate I2C mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  if (s_shared_bus != NULL) {
    initialized = true;
    return ESP_OK;
  }

  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = ARS_I2C_PORT,
      .scl_io_num = ARS_I2C_SCL,
      .sda_io_num = ARS_I2C_SDA,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  ESP_LOGI(TAG, "Init shared I2C bus 0 (SCL=%d SDA=%d)", ARS_I2C_SCL, ARS_I2C_SDA);

  esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &s_shared_bus);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize shared I2C bus: %s",
             esp_err_to_name(ret));
  } else {
    initialized = true;
    ESP_LOGI(TAG, "Shared I2C bus initialized successfully");
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
  if (g_i2c_bus_mutex)
    xSemaphoreGiveRecursive(g_i2c_bus_mutex);
}

i2c_master_bus_handle_t i2c_bus_shared_get_handle(void) { return s_shared_bus; }

bool i2c_bus_shared_is_ready(void) { return s_shared_bus != NULL; }

void i2c_bus_shared_deinit(void) {
  if (s_shared_bus != NULL) {
    esp_err_t del_ret = i2c_del_master_bus(s_shared_bus);
    if (del_ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to delete shared I2C bus: %s",
               esp_err_to_name(del_ret));
    }
    s_shared_bus = NULL;
  }

  if (g_i2c_bus_mutex != NULL) {
    vSemaphoreDelete(g_i2c_bus_mutex);
    g_i2c_bus_mutex = NULL;
  }
}
