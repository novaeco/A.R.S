#pragma once

#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include <lvgl.h>

// Mapping Kconfig to Board Macros
#define BOARD_LCD_HRES 1024
#define BOARD_LCD_VRES 600
#define BOARD_LCD_PCLK_HZ CONFIG_ARS_LCD_PCLK_HZ
#define BOARD_LCD_BIT_PER_PIXEL 16
#define BOARD_LCD_RGB_BIT_PER_PIXEL 16
#define BOARD_LCD_RGB_DATA_WIDTH 16
#define BOARD_LCD_RGB_BUFFER_NUMS 2
#define BOARD_LCD_RGB_BOUNCE_BUFFER_LINES 10
#ifdef CONFIG_ARS_LCD_PCLK_ACTIVE_NEG
#define BOARD_LCD_PCLK_ACTIVE_NEG 1
#else
#define BOARD_LCD_PCLK_ACTIVE_NEG 0
#endif

#ifdef CONFIG_ARS_LCD_HSYNC_IDLE_LOW
#define BOARD_LCD_HSYNC_IDLE_LOW 1
#else
#define BOARD_LCD_HSYNC_IDLE_LOW 0
#endif

#ifdef CONFIG_ARS_LCD_VSYNC_IDLE_LOW
#define BOARD_LCD_VSYNC_IDLE_LOW 1
#else
#define BOARD_LCD_VSYNC_IDLE_LOW 0
#endif

#ifdef CONFIG_ARS_LCD_DE_IDLE_HIGH
#define BOARD_LCD_DE_IDLE_HIGH 1
#else
#define BOARD_LCD_DE_IDLE_HIGH 0
#endif

#define BOARD_LCD_IO_DISP (-1)
#define BOARD_LCD_IO_VSYNC GPIO_NUM_3
#define BOARD_LCD_IO_HSYNC GPIO_NUM_46
#define BOARD_LCD_IO_DE GPIO_NUM_5
#define BOARD_LCD_IO_PCLK GPIO_NUM_7

#define BOARD_LCD_IO_DATA0 GPIO_NUM_14
#define BOARD_LCD_IO_DATA1 GPIO_NUM_38
#define BOARD_LCD_IO_DATA2 GPIO_NUM_18
#define BOARD_LCD_IO_DATA3 GPIO_NUM_17
#define BOARD_LCD_IO_DATA4 GPIO_NUM_10
#define BOARD_LCD_IO_DATA5 GPIO_NUM_39
#define BOARD_LCD_IO_DATA6 GPIO_NUM_0
#define BOARD_LCD_IO_DATA7 GPIO_NUM_45
#define BOARD_LCD_IO_DATA8 GPIO_NUM_48
#define BOARD_LCD_IO_DATA9 GPIO_NUM_47
#define BOARD_LCD_IO_DATA10 GPIO_NUM_21
#define BOARD_LCD_IO_DATA11 GPIO_NUM_1
#define BOARD_LCD_IO_DATA12 GPIO_NUM_2
#define BOARD_LCD_IO_DATA13 GPIO_NUM_42
#define BOARD_LCD_IO_DATA14 GPIO_NUM_41
#define BOARD_LCD_IO_DATA15 GPIO_NUM_40

#ifdef CONFIG_ARS_BAT_ADC_UNIT
#define BOARD_BAT_ADC_UNIT   CONFIG_ARS_BAT_ADC_UNIT
#else
#define BOARD_BAT_ADC_UNIT   ADC_UNIT_1 // Default fallback
#endif

#ifdef CONFIG_ARS_BAT_ADC_CHANNEL
#define BOARD_BAT_ADC_CHAN   CONFIG_ARS_BAT_ADC_CHANNEL
#else
#define BOARD_BAT_ADC_CHAN   0 // Default fallback
#endif

#define BOARD_BAT_DIV_NUM    CONFIG_ARS_BAT_DIV_NUM
#define BOARD_BAT_DIV_DEN    CONFIG_ARS_BAT_DIV_DEN

// I2C routing (shared bus used by GT911 and IO expanders)
#ifdef CONFIG_ARS_I2C_PORT
#define ARS_I2C_PORT CONFIG_ARS_I2C_PORT
#else
#define ARS_I2C_PORT I2C_NUM_0
#endif

#ifdef CONFIG_ARS_I2C_SDA
#define ARS_I2C_SDA CONFIG_ARS_I2C_SDA
#else
#define ARS_I2C_SDA GPIO_NUM_8
#endif

#ifdef CONFIG_ARS_I2C_SCL
#define ARS_I2C_SCL CONFIG_ARS_I2C_SCL
#else
#define ARS_I2C_SCL GPIO_NUM_9
#endif

#ifdef CONFIG_ARS_I2C_FREQUENCY
#define ARS_I2C_FREQUENCY CONFIG_ARS_I2C_FREQUENCY
#else
#define ARS_I2C_FREQUENCY (100 * 1000)
#endif

// Initialize Board Hardware (LCD, Touch, I2C, etc.)
// DOES NOT initialize SD card (moved to app_board_init_sd)
esp_err_t app_board_init(void);

// Initialize SD Card (Blocking or separate task)
esp_err_t app_board_init_sd(void);

// Get the LVGL display object
lv_display_t *app_board_get_disp(void);

// Get the LVGL input device object
lv_indev_t *app_board_get_indev(void);

// Get touch handle (for calibration)
#include "touch.h"
esp_lcd_touch_handle_t app_board_get_touch_handle(void);
esp_lcd_panel_handle_t app_board_get_panel_handle(void);

// Run HW diagnostics
void app_board_run_diagnostics(void);

// Check if SD card is currently mounted
bool board_sd_is_mounted(void);

// Set backlight brightness (0-100)
void board_set_backlight_percent(uint8_t percent);
// Get battery level (0-100%) and voltage in mV
esp_err_t board_get_battery_level(uint8_t *percent, uint16_t *voltage_mv);

// Direct Test
void board_lcd_test_pattern(void);
