#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "driver/spi_common.h"

// Shared I2C bus
#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 9
#define BOARD_I2C_PORT I2C_NUM_0

// IO expander
#define BOARD_IO_EXT_ADDR 0x24
#define BOARD_IO_EXT_PIN_TOUCH_IRQ 0
#define BOARD_IO_EXT_PIN_TOUCH_RST 1
#define BOARD_IO_EXT_PIN_BACKLIGHT 2
#define BOARD_IO_EXT_PIN_SD_CS 4
#define BOARD_IO_EXT_PIN_LCD_POWER 6

// Touch GT911
#define BOARD_TOUCH_IRQ_GPIO 4
#define BOARD_TOUCH_RST_EXIO BOARD_IO_EXT_PIN_TOUCH_RST

// SD card (SDSPI)
#define BOARD_SD_CS_EXIO BOARD_IO_EXT_PIN_SD_CS
#define BOARD_SD_SPI_HOST SPI2_HOST
#define BOARD_SD_SPI_MOSI 11
#define BOARD_SD_SPI_MISO 13
#define BOARD_SD_SPI_SCLK 12
#define BOARD_SD_DUMMY_CS 6

// Display timings and pins (RGB 1024x600)
#define BOARD_DISPLAY_H_RES 1024
#define BOARD_DISPLAY_V_RES 600
#define BOARD_DISPLAY_PCLK_HZ 16000000

#define BOARD_LCD_VSYNC 3
#define BOARD_LCD_HSYNC 46
#define BOARD_LCD_DE 5
#define BOARD_LCD_PCLK 7

#define BOARD_LCD_DATA0 0   // G3
#define BOARD_LCD_DATA1 1   // R3
#define BOARD_LCD_DATA2 2   // R4
#define BOARD_LCD_DATA3 10  // B7
#define BOARD_LCD_DATA4 14  // B3
#define BOARD_LCD_DATA5 17  // B6
#define BOARD_LCD_DATA6 18  // B5
#define BOARD_LCD_DATA7 21  // G7
#define BOARD_LCD_DATA8 38  // B4
#define BOARD_LCD_DATA9 39  // G2
#define BOARD_LCD_DATA10 40 // R7
#define BOARD_LCD_DATA11 41 // R6
#define BOARD_LCD_DATA12 42 // R5
#define BOARD_LCD_DATA13 45 // G4
#define BOARD_LCD_DATA14 47 // G6
#define BOARD_LCD_DATA15 48 // G5

// Display power rails via IO expander
#define BOARD_LCD_BACKLIGHT_EXIO BOARD_IO_EXT_PIN_BACKLIGHT
#define BOARD_LCD_POWER_EXIO BOARD_IO_EXT_PIN_LCD_POWER
