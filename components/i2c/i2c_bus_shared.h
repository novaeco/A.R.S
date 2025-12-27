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
 * Known I2C device addresses on the shared bus (Waveshare ESP32-S3 7B)
 * These are for documentation/reference; actual addresses defined in drivers.
 */
#define I2C_ADDR_IO_EXTENSION 0x24  // CH32V003 IO extension
#define I2C_ADDR_GT911_PRIMARY 0x5D // GT911 touch primary address
#define I2C_ADDR_GT911_BACKUP 0x14  // GT911 touch backup address

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
 * @brief Convenience wrappers using millisecond timeouts for global I2C mutex.
 */
bool ars_i2c_lock(uint32_t timeout_ms);
void ars_i2c_unlock(void);

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
esp_err_t i2c_bus_shared_recover(const char *tag);

/**
 * @brief Attempt bus recovery when caller ALREADY holds the mutex.
 *        Use this from code paths that have locked the bus.
 *
 * @note Caller MUST already hold the I2C bus lock before calling this.
 */
esp_err_t i2c_bus_shared_recover_locked(const char *tag);

/**
 * @brief Force bus recovery without backoff throttling.
 *
 * Use for critical paths (e.g., SD ext-CS) that must clear INVALID_RESPONSE
 * bursts immediately. Safe only from task context.
 */
esp_err_t i2c_bus_shared_recover_force(const char *tag);

/**
 * @brief Force bus recovery (caller already holds the I2C mutex).
 */
esp_err_t i2c_bus_shared_recover_locked_force(const char *tag);

/**
 * @brief Check if current task holds the I2C mutex.
 *        Useful for recovery paths to determine if locked variant should be
 * used.
 *
 * @return true if current task holds the mutex, false otherwise.
 */
bool i2c_bus_shared_is_locked_by_me(void);

/**
 * @brief Track a successful I2C transaction on the shared bus.
 *
 * Resets consecutive-error streak and gradually relaxes any backoff that may
 * have been introduced after previous errors.
 */
void i2c_bus_shared_note_success(void);

/**
 * @brief Track an I2C error on the shared bus.
 *
 * @param ctx Short context string (component/tag) for diagnostics.
 * @param err esp_err_t returned by the failing operation.
 *
 * Increments error counters and updates a dynamic backoff hint that callers
 * can use to slow down polling when the bus is unhealthy.
 */
void i2c_bus_shared_note_error(const char *ctx, esp_err_t err);

/**
 * @brief Return the current consecutive-error streak.
 */
uint32_t i2c_bus_shared_get_error_streak(void);

/**
 * @brief Suggested delay (ticks) after an error burst to ease bus contention.
 *
 * The hint decays automatically when calls to i2c_bus_shared_note_success() are
 * observed.
 */
TickType_t i2c_bus_shared_backoff_ticks(void);

#ifdef __cplusplus
}
#endif
