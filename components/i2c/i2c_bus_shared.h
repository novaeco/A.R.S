#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global I2C Mutex for Shared Bus (I2C0)
extern SemaphoreHandle_t g_i2c_bus_mutex;

/**
 * @brief Initialize the Shared I2C Bus (Port 0) and the global mutex.
 *        Safe to call multiple times (idempotent).
 *        Pins: SDA=8, SCL=9
 *        Speed: 400kHz
 */
void i2c_bus_shared_init(void);

/**
 * @brief Acquire the shared I2C bus mutex.
 *
 * @param timeout_ticks Timeout in FreeRTOS ticks.
 * @return true on success, false if the lock could not be taken.
 */
bool i2c_bus_shared_lock(TickType_t timeout_ticks);

/**
 * @brief Release the shared I2C bus mutex.
 */
void i2c_bus_shared_unlock(void);

#ifdef __cplusplus
}
#endif
