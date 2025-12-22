#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
esp_err_t i2c_bus_shared_init(void);

/**
 * @brief Release shared I2C resources (mutex and bus handle) when no user
 *        remains. Intended for test teardown.
 */
void i2c_bus_shared_deinit(void);

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

/**
 * @brief Get the shared I2C master bus handle once initialized.
 */
i2c_master_bus_handle_t i2c_bus_shared_get_handle(void);

/**
 * @brief Quick readiness probe for components that require a valid bus handle.
 */
bool i2c_bus_shared_is_ready(void);

/**
 * @brief Attempt to recover a stuck I2C bus by toggling SCL and reinitializing
 *        the master if needed.
 *
 * Safe to call from task context only (not ISR). Uses the shared mutex.
 */
esp_err_t i2c_bus_shared_recover(void);

/**
 * @brief Attempt bus recovery when caller ALREADY holds the mutex.
 *        Use this from code paths that have locked the bus.
 *
 * @note Caller MUST already hold the I2C bus lock before calling this.
 */
esp_err_t i2c_bus_shared_recover_locked(void);

/**
 * @brief Check if current task holds the I2C mutex.
 *        Useful for recovery paths to determine if locked variant should be
 * used.
 *
 * @return true if current task holds the mutex, false otherwise.
 */
bool i2c_bus_shared_is_locked_by_me(void);

#ifdef __cplusplus
}
#endif
