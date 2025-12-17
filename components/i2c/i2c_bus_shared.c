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

void i2c_bus_shared_init(void) {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  ESP_LOGI(TAG, "Init shared I2C bus 0");

  // Create Global Mutex if not exists
  if (g_i2c_bus_mutex == NULL) {
    g_i2c_bus_mutex = xSemaphoreCreateRecursiveMutex();
    assert(g_i2c_bus_mutex != NULL);
  }

  // Initialize the underlying I2C Master Bus (New Driver) using existing helper
  // This ensures s_bus_handle in i2c.c is valid for GT911 and others
  // and avoids "Driver already installed" errors if mixed.
  i2c_master_bus_handle_t handle = NULL;
  esp_err_t ret = DEV_I2C_Init_Bus(&handle);
  s_shared_bus = handle;

  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize shared I2C bus: %s", esp_err_to_name(ret));
      // We don't abort here, but functionality will be degraded.
  } else {
      ESP_LOGI(TAG, "Shared I2C bus initialized successfully");
  }
}

bool i2c_bus_shared_lock(TickType_t timeout_ticks) {
  if (g_i2c_bus_mutex == NULL) {
    // Ensure mutex exists even if init has not been called explicitly yet
    i2c_bus_shared_init();
  }

  if (g_i2c_bus_mutex == NULL)
    return false;

  return xSemaphoreTakeRecursive(g_i2c_bus_mutex, timeout_ticks) == pdTRUE;
}

void i2c_bus_shared_unlock(void) {
  if (g_i2c_bus_mutex)
    xSemaphoreGiveRecursive(g_i2c_bus_mutex);
}

i2c_master_bus_handle_t i2c_bus_shared_get_handle(void) { return s_shared_bus; }

bool i2c_bus_shared_is_ready(void) { return s_shared_bus != NULL; }
