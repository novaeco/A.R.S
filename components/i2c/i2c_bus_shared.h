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

// Shared-bus authoritative addresses (Waveshare ESP32-S3 Touch LCD 7B)
#define I2C_ADDR_IO_EXTENSION 0x24  // CH32V003 IO extension
#define I2C_ADDR_GT911_PRIMARY 0x5D // GT911 touch primary address
#define I2C_ADDR_GT911_BACKUP 0x14  // GT911 touch backup address

typedef struct {
  i2c_port_t port;
  gpio_num_t sda_io_num;
  gpio_num_t scl_io_num;
  uint32_t clk_speed_hz;
  TickType_t mutex_timeout_ticks;
} i2c_bus_shared_config_t;

// Defaults aligned with Waveshare 7B wiring (SDA=GPIO8, SCL=GPIO9)
#ifndef I2C_BUS_SHARED_DEFAULT_SDA
#define I2C_BUS_SHARED_DEFAULT_SDA GPIO_NUM_8
#endif

#ifndef I2C_BUS_SHARED_DEFAULT_SCL
#define I2C_BUS_SHARED_DEFAULT_SCL GPIO_NUM_9
#endif

#ifndef I2C_BUS_SHARED_DEFAULT_PORT
#define I2C_BUS_SHARED_DEFAULT_PORT I2C_NUM_0
#endif

#ifndef I2C_BUS_SHARED_DEFAULT_CLK_HZ
#define I2C_BUS_SHARED_DEFAULT_CLK_HZ (400 * 1000)
#endif

#ifndef I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS
#define I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS pdMS_TO_TICKS(100)
#endif

/**
 * @brief Initialize the shared I2C bus and mutex (idempotent).
 *
 * When cfg is NULL, default pins (SDA=8, SCL=9), port 0 and 400kHz are used.
 */
esp_err_t i2c_bus_shared_init_with_config(const i2c_bus_shared_config_t *cfg);

/**
 * @brief Initialize the shared I2C bus using default configuration.
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
i2c_master_bus_handle_t i2c_bus_shared_get_bus(void);

/**
 * @brief Expose the shared mutex handle for diagnostics/tests.
 */
SemaphoreHandle_t i2c_bus_shared_get_mutex(void);

/**
 * @brief Register an I2C device on the shared bus with address guard.
 *
 * Fails with ESP_ERR_INVALID_STATE if the address is already registered.
 */
esp_err_t i2c_bus_shared_add_device(uint8_t addr7, uint32_t scl_hz,
                                    i2c_master_dev_handle_t *out_dev);

/**
 * @brief Remove a device handle previously added via i2c_bus_shared_add_device
 *        and free the address slot.
 */
esp_err_t i2c_bus_shared_remove_device(i2c_master_dev_handle_t dev_handle);

/**
 * @brief Reserve an address on the shared bus (no device handle returned).
 *        Use for drivers that create their own panel IO handles.
 */
esp_err_t i2c_bus_shared_reserve_address(uint8_t addr7);

/**
 * @brief Release a previously reserved address (without device handle).
 */
esp_err_t i2c_bus_shared_release_address(uint8_t addr7);

/**
 * @brief Probe a 7-bit address on the shared bus using the shared mutex.
 */
esp_err_t i2c_bus_shared_probe(uint8_t addr7, TickType_t timeout_ticks);

/**
 * @brief Serialized transmit+receive transaction.
 */
esp_err_t i2c_bus_shared_txrx(i2c_master_dev_handle_t dev,
                              const uint8_t *tx_buffer, size_t tx_len,
                              uint8_t *rx_buffer, size_t rx_len,
                              TickType_t timeout_ticks);

/**
 * @brief Serialized transmit-only transaction.
 */
esp_err_t i2c_bus_shared_tx(i2c_master_dev_handle_t dev,
                            const uint8_t *tx_buffer, size_t tx_len,
                            TickType_t timeout_ticks);

/**
 * @brief Serialized receive-only transaction.
 */
esp_err_t i2c_bus_shared_rx(i2c_master_dev_handle_t dev, uint8_t *rx_buffer,
                            size_t rx_len, TickType_t timeout_ticks);

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

#define i2c_bus_shared_get_handle() i2c_bus_shared_get_bus()

#ifdef __cplusplus
}
#endif
