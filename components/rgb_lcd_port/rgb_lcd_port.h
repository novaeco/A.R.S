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
#define ARS_LCD_H_RES BOARD_LCD_HRES ///< Horizontal resolution in pixels
#define ARS_LCD_V_RES BOARD_LCD_VRES ///< Vertical resolution in pixels
#define ARS_LCD_PIXEL_CLOCK_HZ                                                 \
  BOARD_LCD_PCLK_HZ ///< Pixel clock frequency in Hz (configurable, 51.2 MHz
                    ///< recommandé 1024x600@60Hz)

/**
 * @brief Color and Pixel Configuration
 */
#define ARS_LCD_BIT_PER_PIXEL                                                  \
  BOARD_LCD_BIT_PER_PIXEL ///< Bits per pixel (color depth)
#define ARS_RGB_BIT_PER_PIXEL                                                  \
  BOARD_LCD_RGB_BIT_PER_PIXEL ///< RGB interface color depth
#define ARS_RGB_DATA_WIDTH                                                     \
  BOARD_LCD_RGB_DATA_WIDTH ///< Data width for RGB interface
#define ARS_LCD_RGB_BUFFER_NUMS                                                \
  BOARD_LCD_RGB_BUFFER_NUMS ///< Number of frame buffers for double buffering
#define ARS_RGB_BOUNCE_BUFFER_SIZE                                             \
  (ARS_LCD_H_RES *                                                             \
   BOARD_LCD_RGB_BOUNCE_BUFFER_LINES) ///< Size of bounce buffer for RGB data

/**
 * @brief GPIO Pins for RGB LCD Signals
 */
#define ARS_LCD_IO_RGB_DISP BOARD_LCD_IO_DISP   ///< DISP signal, -1 if not used
#define ARS_LCD_IO_RGB_VSYNC BOARD_LCD_IO_VSYNC ///< Vertical sync signal
#define ARS_LCD_IO_RGB_HSYNC BOARD_LCD_IO_HSYNC ///< Horizontal sync signal
#define ARS_LCD_IO_RGB_DE BOARD_LCD_IO_DE       ///< Data enable signal
#define ARS_LCD_IO_RGB_PCLK BOARD_LCD_IO_PCLK   ///< Pixel clock signal

/**
 * @brief GPIO Pins for RGB Data Signals
 */
// Blue data signals
#define ARS_LCD_IO_RGB_DATA0 BOARD_LCD_IO_DATA0 ///< B3
#define ARS_LCD_IO_RGB_DATA1 BOARD_LCD_IO_DATA1 ///< B4
#define ARS_LCD_IO_RGB_DATA2 BOARD_LCD_IO_DATA2 ///< B5
#define ARS_LCD_IO_RGB_DATA3 BOARD_LCD_IO_DATA3 ///< B6
#define ARS_LCD_IO_RGB_DATA4 BOARD_LCD_IO_DATA4 ///< B7

// Green data signals
#define ARS_LCD_IO_RGB_DATA5 BOARD_LCD_IO_DATA5   ///< G2
#define ARS_LCD_IO_RGB_DATA6 BOARD_LCD_IO_DATA6   ///< G3
#define ARS_LCD_IO_RGB_DATA7 BOARD_LCD_IO_DATA7   ///< G4
#define ARS_LCD_IO_RGB_DATA8 BOARD_LCD_IO_DATA8   ///< G5
#define ARS_LCD_IO_RGB_DATA9 BOARD_LCD_IO_DATA9   ///< G6
#define ARS_LCD_IO_RGB_DATA10 BOARD_LCD_IO_DATA10 ///< G7

// Red data signals
#define ARS_LCD_IO_RGB_DATA11 BOARD_LCD_IO_DATA11 ///< R3
#define ARS_LCD_IO_RGB_DATA12 BOARD_LCD_IO_DATA12 ///< R4
#define ARS_LCD_IO_RGB_DATA13 BOARD_LCD_IO_DATA13 ///< R5
#define ARS_LCD_IO_RGB_DATA14 BOARD_LCD_IO_DATA14 ///< R6
#define ARS_LCD_IO_RGB_DATA15 BOARD_LCD_IO_DATA15 ///< R7

/**
 * @brief Reset and Backlight Configuration
 */
#define ARS_LCD_IO_RST (-1)           ///< Reset pin, -1 if not used
#define ARS_PIN_NUM_BK_LIGHT (-1)     ///< Backlight pin, -1 if not used
#define ARS_LCD_BK_LIGHT_ON_LEVEL (1) ///< Logic level to turn on backlight
#define ARS_LCD_BK_LIGHT_OFF_LEVEL                                             \
  (!ARS_LCD_BK_LIGHT_ON_LEVEL) ///< Logic level to turn off backlight

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

/**
 * @brief Baisse temporaire du PCLK autour d'opérations sensibles.
 *
 * Quand CONFIG_ARS_LCD_PCLK_GUARD_ENABLE=y, réduit la fréquence PCLK à la
 * valeur CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ, attend un délai de stabilisation,
 * puis laisse l'appelant exécuter l'opération critique.
 *
 * @param reason Texte optionnel pour les logs (peut être NULL).
 * @param applied_hz Si non NULL, reçoit la fréquence appliquée.
 */
esp_err_t rgb_lcd_port_pclk_guard_enter(const char *reason,
                                        uint32_t *applied_hz);

/**
 * @brief Restaure le PCLK nominal après rgb_lcd_port_pclk_guard_enter().
 */
esp_err_t rgb_lcd_port_pclk_guard_exit(void);
#endif // _RGB_LCD_H_
