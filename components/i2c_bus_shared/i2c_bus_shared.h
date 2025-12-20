#pragma once
#include "esp_err.h"
#include "driver/i2c.h"
#include "freertos/semphr.h"

esp_err_t i2c_bus_shared_init(gpio_num_t sda_io, gpio_num_t scl_io);
SemaphoreHandle_t i2c_bus_shared_get_mutex(void);
i2c_port_t i2c_bus_shared_port(void);
esp_err_t i2c_bus_shared_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t i2c_bus_shared_write(uint8_t addr, const uint8_t *data, size_t len);
