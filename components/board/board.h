#pragma once
#include "esp_err.h"

#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 9
#define BOARD_I2C_PORT I2C_NUM_0

#define BOARD_IO_EXT_ADDR 0x24

#define BOARD_TOUCH_IRQ_IO_EXT_PIN 0
#define BOARD_TOUCH_RST_IO_EXT_PIN 1

#define BOARD_SD_CS_IO_EXT_PIN 2

#define BOARD_DISPLAY_H_RES 1024
#define BOARD_DISPLAY_V_RES 600

esp_err_t board_init(void);
