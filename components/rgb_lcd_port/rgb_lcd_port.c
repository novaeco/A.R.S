/*****************************************************************************
 * | File      	 :   rgb_lcd_port.c
 * | Author      :   Waveshare team
 * | Function    :   Hardware underlying interface
 * | Info        :
 *                   RGB LCD driver code
 *----------------
 * |This version :   V1.0
 * | Date        :   2024-11-19
 * | Info        :   Basic version
 *
 ******************************************************************************/

#include "rgb_lcd_port.h"
#include "esp_idf_version.h"
#include "esp_lcd_types.h"
#include "io_extension.h"
#include <stdbool.h>

__attribute__((weak)) bool lvgl_port_notify_rgb_vsync(void);

const char *TAG = "rgb_lcd";

// Handle for the RGB LCD panel
static esp_lcd_panel_handle_t panel_handle =
    NULL; // Declare a handle for the LCD panel

// Frame buffer complete event callback function
IRAM_ATTR static bool
rgb_lcd_on_frame_buf_complete_event(esp_lcd_panel_handle_t panel,
                                    const esp_lcd_rgb_panel_event_data_t *edata,
                                    void *user_ctx) {
  if (lvgl_port_notify_rgb_vsync) {
    return lvgl_port_notify_rgb_vsync();
  }
  return false;
}

/**
 * @brief Initialize the RGB LCD panel on the ESP32-S3
 *
 * This function configures and initializes an RGB LCD panel driver
 * using the ESP-IDF RGB LCD driver API. It sets up timing parameters,
 * GPIOs, data width, and framebuffer settings for the LCD panel.
 *
 * @return
 *    - ESP_OK: Initialization successful.
 *    - Other error codes: Initialization failed.
 */
esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_init() {
  // Log the start of the RGB LCD panel driver installation
  ESP_LOGI(TAG, "Install RGB LCD panel driver");

  // Configuration structure for the RGB LCD panel
  esp_lcd_rgb_panel_config_t panel_config = {
      .clk_src = LCD_CLK_SRC_DEFAULT, // Use the default clock source
      .timings =
          {
              .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ, // Pixel clock frequency
                                                     // (18MHz for stable
                                                     // 1024x600 on S3)
              .h_res = EXAMPLE_LCD_H_RES,            // 1024
              .v_res = EXAMPLE_LCD_V_RES,            // 600
              // ST7262 Wavershare 7B Timings (1024x600)
              // H: Pulse=20, Back=140, Front=160
              // V: Pulse=3, Back=12, Front=12
              .hsync_pulse_width = 20,
              .hsync_back_porch = 140,
              .hsync_front_porch = 160,
              .vsync_pulse_width = 3,
              .vsync_back_porch = 12,
              .vsync_front_porch = 12,
              .flags =
                  {
                      .pclk_active_neg =
                          1, // PCLK on Falling Edge (Vital for ST7262)
                  },
          },
      .data_width = EXAMPLE_RGB_DATA_WIDTH, // Data width for RGB signals
      #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
      .in_color_format = LCD_COLOR_FMT_RGB565,
      #else
      .bits_per_pixel = 16,
      #endif
      .num_fbs = EXAMPLE_LCD_RGB_BUFFER_NUMS, // Number of framebuffers for
                                              // double/triple buffering
      .bounce_buffer_size_px =
          EXAMPLE_RGB_BOUNCE_BUFFER_SIZE, // Bounce buffer size in pixels
      .dma_burst_size = 64,               // DMA burst size
      .hsync_gpio_num =
          EXAMPLE_LCD_IO_RGB_HSYNC, // GPIO for horizontal sync signal
      .vsync_gpio_num =
          EXAMPLE_LCD_IO_RGB_VSYNC,             // GPIO for vertical sync signal
      .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,     // GPIO for data enable signal
      .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK, // GPIO for pixel clock signal
      .disp_gpio_num =
          EXAMPLE_LCD_IO_RGB_DISP, // GPIO for display enable signal
      .data_gpio_nums =
          {
              // GPIOs for RGB data signals
              EXAMPLE_LCD_IO_RGB_DATA0,  // Data bit 0
              EXAMPLE_LCD_IO_RGB_DATA1,  // Data bit 1
              EXAMPLE_LCD_IO_RGB_DATA2,  // Data bit 2
              EXAMPLE_LCD_IO_RGB_DATA3,  // Data bit 3
              EXAMPLE_LCD_IO_RGB_DATA4,  // Data bit 4
              EXAMPLE_LCD_IO_RGB_DATA5,  // Data bit 5
              EXAMPLE_LCD_IO_RGB_DATA6,  // Data bit 6
              EXAMPLE_LCD_IO_RGB_DATA7,  // Data bit 7
              EXAMPLE_LCD_IO_RGB_DATA8,  // Data bit 8
              EXAMPLE_LCD_IO_RGB_DATA9,  // Data bit 9
              EXAMPLE_LCD_IO_RGB_DATA10, // Data bit 10
              EXAMPLE_LCD_IO_RGB_DATA11, // Data bit 11
              EXAMPLE_LCD_IO_RGB_DATA12, // Data bit 12
              EXAMPLE_LCD_IO_RGB_DATA13, // Data bit 13
              EXAMPLE_LCD_IO_RGB_DATA14, // Data bit 14
              EXAMPLE_LCD_IO_RGB_DATA15, // Data bit 15
          },
      .flags =
          {
              .fb_in_psram =
                  1, // Use PSRAM for framebuffers to save internal SRAM
          },
  };

  // Create and register the RGB LCD panel driver with the configuration above
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

  // Log the initialization of the RGB LCD panel
  ESP_LOGI(TAG, "Initialize RGB LCD panel");

  // Initialize the RGB LCD panel
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

  esp_lcd_rgb_panel_event_callbacks_t cbs = {
      .on_frame_buf_complete =
          rgb_lcd_on_frame_buf_complete_event, // Callback for frame buffer
                                               // complete
      .on_vsync = rgb_lcd_on_frame_buf_complete_event, // Also trigger on VSYNC
  };
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(
      panel_handle, &cbs, NULL)); // Register event callbacks

  return panel_handle;
}

/**
 * @brief Get the handle of the initialized RGB LCD panel.
 * @return Handle of the RGB LCD panel, or NULL if not initialized.
 */
esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_get_handle(void) {
  return panel_handle;
}

/**
 * @brief Turn on the RGB LCD screen backlight.
 *
 * This function enables the backlight of the screen by configuring the IO
 * EXTENSION I/O expander to output mode and setting the backlight pin to high.
 * The IO EXTENSION is controlled via I2C.
 *
 * @return
 */
void waveshare_rgb_lcd_bl_on() {
  IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1); // Backlight ON configuration
}

/**
 * @brief Turn off the RGB LCD screen backlight.
 *
 * This function disables the backlight of the screen by configuring the IO
 * EXTENSION I/O expander to output mode and setting the backlight pin to low.
 * The IO EXTENSION is controlled via I2C.
 *
 * @return
 */
void waveshare_rgb_lcd_bl_off() {
  IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0); // Backlight OFF configuration
}
