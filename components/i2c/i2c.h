/*****************************************************************************
 * | File         :   i2c.h
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 I2C driver code for I2C communication.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-26
 * | Info         :   Basic version
 *
 ******************************************************************************/

#ifndef __I2C_H
#define __I2C_H

#include "board.h"          // Pinout and frequency
#include "i2c_bus_shared.h" // Global Mutex & Init

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  i2c_master_bus_handle_t bus;
  i2c_master_dev_handle_t dev;
} DEV_I2C_Port;

/**
 * @brief Initialize the global I2C master bus (Singleton).
 *
 * Checks if the bus is already initialized. If so, returns the existing handle.
 * If not, creates a new bus instance.
 *
 * @param out_p_bus_handle Pointer to store the bus handle.
 * @return ESP_OK on success.
 */
esp_err_t DEV_I2C_Init_Bus(i2c_master_bus_handle_t *out_p_bus_handle);

/**
 * @brief Add a device to the shared I2C bus.
 *
 * Ensures 7-bit address sanitation.
 *
 * @param addr I2C address (will be sanitized).
 * @param out_dev_handle Pointer to store the new device handle.
 * @return ESP_OK on success.
 */
esp_err_t DEV_I2C_Add_Device(uint8_t addr,
                             i2c_master_dev_handle_t *out_dev_handle);

/**
 * @brief Initialize legacy DEV_I2C_Port structure (for backward compatibility).
 *        Prefer calling DEV_I2C_Init_Bus/DEV_I2C_Add_Device directly in new
 *        code to avoid duplicate init paths.
 */
esp_err_t DEV_I2C_Init(DEV_I2C_Port *out_port);

// --- Bus Safety & Recovery ---

/**
 * @brief Validate a 7-bit I2C address and return it through out parameter.
 * Returns ESP_ERR_INVALID_ARG if the address is outside the 7-bit range.
 */
esp_err_t DEV_I2C_SanitizeAddr(uint8_t addr, uint8_t *out_addr);

/**
 * @brief Probe for a device on the given bus and address.
 * Uses the shared bus mutex to serialize access.
 */
esp_err_t DEV_I2C_Probe(i2c_master_bus_handle_t bus_handle, uint8_t addr);

/**
 * @brief Take the global I2C mutex.
 * Call this before a sequence of atomic I2C operations if needed,
 * though individual Read/Write functions will also lock it.
 */
bool DEV_I2C_TakeLock(TickType_t wait_ms);

/**
 * @brief Give the global I2C mutex.
 */
void DEV_I2C_GiveLock(void);

/**
 * @brief Attempt to recover the I2C bus from a stuck state.
 * Non-destructive: Toggles SCL/SDA to release slave, does NOT delete bus
 * driver.
 */
esp_err_t DEV_I2C_BusReset(void);

// --- Standard Read/Write (Mutex Protected) ---

esp_err_t DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *dev_handle,
                                 uint8_t Addr);
esp_err_t DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t value);
esp_err_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle, uint8_t *value);
esp_err_t DEV_I2C_Read_Word(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                            uint16_t *value);
esp_err_t DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t dev_handle,
                              uint8_t *pdata, uint8_t len);
esp_err_t DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t *pdata, uint8_t len);

#endif
