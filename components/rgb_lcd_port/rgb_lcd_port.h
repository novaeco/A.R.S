/*****************************************************************************
 * | File        :   rgb_lcd_port.h
 * | Author      :   Waveshare team
 * | Function    :   Hardware underlying interface
 * | Info        :
 *                   This header file contains configuration and function
 *                   declarations for the RGB LCD driver interface.
 *----------------
 * | Version     :   V1.0
 * | Date        :   2024-11-19
 * | Info        :   Basic version
 *
 ******************************************************************************/

#ifndef _RGB_LCD_H_
#define _RGB_LCD_H_

#pragma once

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stddef.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your
/// LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief LCD Resolution and Timing
 */
#define EXAMPLE_LCD_H_RES BOARD_LCD_HRES ///< Horizontal resolution in pixels
#define EXAMPLE_LCD_V_RES BOARD_LCD_VRES ///< Vertical resolution in pixels
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ                                            \
  BOARD_LCD_PCLK_HZ ///< Pixel clock frequency in Hz (configurable, 51.2 MHz recommandé 1024x600@60Hz)

/**
 * @brief Color and Pixel Configuration
 */
#define EXAMPLE_LCD_BIT_PER_PIXEL BOARD_LCD_BIT_PER_PIXEL ///< Bits per pixel (color depth)
#define EXAMPLE_RGB_BIT_PER_PIXEL                                                     \
  BOARD_LCD_RGB_BIT_PER_PIXEL ///< RGB interface color depth
#define EXAMPLE_RGB_DATA_WIDTH BOARD_LCD_RGB_DATA_WIDTH ///< Data width for RGB interface
#define EXAMPLE_LCD_RGB_BUFFER_NUMS                                                      \
  BOARD_LCD_RGB_BUFFER_NUMS ///< Number of frame buffers for double buffering
#define EXAMPLE_RGB_BOUNCE_BUFFER_SIZE                                                   \
  (EXAMPLE_LCD_H_RES * BOARD_LCD_RGB_BOUNCE_BUFFER_LINES) ///< Size of bounce buffer for RGB data

/**
 * @brief GPIO Pins for RGB LCD Signals
 */
#define EXAMPLE_LCD_IO_RGB_DISP BOARD_LCD_IO_DISP ///< DISP signal, -1 if not used
#define EXAMPLE_LCD_IO_RGB_VSYNC BOARD_LCD_IO_VSYNC ///< Vertical sync signal
#define EXAMPLE_LCD_IO_RGB_HSYNC BOARD_LCD_IO_HSYNC ///< Horizontal sync signal
#define EXAMPLE_LCD_IO_RGB_DE BOARD_LCD_IO_DE ///< Data enable signal
#define EXAMPLE_LCD_IO_RGB_PCLK BOARD_LCD_IO_PCLK ///< Pixel clock signal

/**
 * @brief GPIO Pins for RGB Data Signals
 */
// Blue data signals
#define EXAMPLE_LCD_IO_RGB_DATA0 BOARD_LCD_IO_DATA0 ///< B3
#define EXAMPLE_LCD_IO_RGB_DATA1 BOARD_LCD_IO_DATA1 ///< B4
#define EXAMPLE_LCD_IO_RGB_DATA2 BOARD_LCD_IO_DATA2 ///< B5
#define EXAMPLE_LCD_IO_RGB_DATA3 BOARD_LCD_IO_DATA3 ///< B6
#define EXAMPLE_LCD_IO_RGB_DATA4 BOARD_LCD_IO_DATA4 ///< B7

// Green data signals
#define EXAMPLE_LCD_IO_RGB_DATA5 BOARD_LCD_IO_DATA5 ///< G2
#define EXAMPLE_LCD_IO_RGB_DATA6 BOARD_LCD_IO_DATA6 ///< G3
#define EXAMPLE_LCD_IO_RGB_DATA7 BOARD_LCD_IO_DATA7 ///< G4
#define EXAMPLE_LCD_IO_RGB_DATA8 BOARD_LCD_IO_DATA8 ///< G5
#define EXAMPLE_LCD_IO_RGB_DATA9 BOARD_LCD_IO_DATA9 ///< G6
#define EXAMPLE_LCD_IO_RGB_DATA10 BOARD_LCD_IO_DATA10 ///< G7

// Red data signals
#define EXAMPLE_LCD_IO_RGB_DATA11 BOARD_LCD_IO_DATA11 ///< R3
#define EXAMPLE_LCD_IO_RGB_DATA12 BOARD_LCD_IO_DATA12 ///< R4
#define EXAMPLE_LCD_IO_RGB_DATA13 BOARD_LCD_IO_DATA13 ///< R5
#define EXAMPLE_LCD_IO_RGB_DATA14 BOARD_LCD_IO_DATA14 ///< R6
#define EXAMPLE_LCD_IO_RGB_DATA15 BOARD_LCD_IO_DATA15 ///< R7

/**
 * @brief Reset and Backlight Configuration
 */
#define EXAMPLE_LCD_IO_RST (-1)           ///< Reset pin, -1 if not used
#define EXAMPLE_PIN_NUM_BK_LIGHT (-1)     ///< Backlight pin, -1 if not used
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL (1) ///< Logic level to turn on backlight
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL                                         \
  (!EXAMPLE_LCD_BK_LIGHT_ON_LEVEL) ///< Logic level to turn off backlight

/**
 * @brief Function Declarations
 */
esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_init();

/**
 * @brief Get the handle of the initialized RGB LCD panel.
 */
esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_get_handle(void);

/**
 * @brief Récupère les framebuffers alloués par le driver RGB
 *
 * @param[out] buffers tableau interne des pointeurs de buffers
 * @param[out] buffer_count nombre de buffers valides
 * @param[out] stride_bytes taille en octets d'une ligne
 */
esp_err_t rgb_lcd_port_get_framebuffers(void ***buffers, size_t *buffer_count,
                                       size_t *stride_bytes);

void waveshare_rgb_lcd_bl_on();
void waveshare_rgb_lcd_bl_off();
#endif // _RGB_LCD_H_
