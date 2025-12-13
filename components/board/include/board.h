#pragma once

#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

// Mapping Kconfig to Board Macros
#ifdef CONFIG_ARS_BAT_ADC_UNIT
#define BOARD_BAT_ADC_UNIT   CONFIG_ARS_BAT_ADC_UNIT
#else
#define BOARD_BAT_ADC_UNIT   ADC_UNIT_1 // Default fallback
#endif

#ifdef CONFIG_ARS_BAT_ADC_CHAN
#define BOARD_BAT_ADC_CHAN   CONFIG_ARS_BAT_ADC_CHAN
#else
#define BOARD_BAT_ADC_CHAN   0 // Default fallback
#endif

#ifdef CONFIG_ARS_BAT_DIVIDER
#define BOARD_BAT_DIVIDER    CONFIG_ARS_BAT_DIVIDER
#else
#define BOARD_BAT_DIVIDER    2.0f // Default fallback
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
