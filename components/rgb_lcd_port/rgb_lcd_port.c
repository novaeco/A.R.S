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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "io_extension.h"
#include "sdkconfig.h"
#include <stdbool.h>

// Force double buffering to ensure smooth UI and prevent "invalid frame buffer"
// errors
#undef ARS_LCD_RGB_BUFFER_NUMS
#define ARS_LCD_RGB_BUFFER_NUMS 2

#if defined(CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE) &&                               \
    CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE
#define ARS_LCD_WAIT_VSYNC_ENABLED CONFIG_ARS_LCD_WAIT_VSYNC
#elif defined(CONFIG_ARS_LCD_WAIT_VSYNC)
#define ARS_LCD_WAIT_VSYNC_ENABLED CONFIG_ARS_LCD_WAIT_VSYNC
#else
#define ARS_LCD_WAIT_VSYNC_ENABLED 0
#endif

__attribute__((weak)) bool lvgl_port_notify_rgb_vsync(void);

const char *TAG = "rgb_lcd";

// Handle for the RGB LCD panel
static esp_lcd_panel_handle_t panel_handle =
    NULL; // Declare a handle for the LCD panel
static void *s_framebuffers[ARS_LCD_RGB_BUFFER_NUMS] = {0};
static size_t s_framebuffer_count = 0;
static size_t s_stride_bytes = 0;
static SemaphoreHandle_t s_pclk_guard_lock = NULL;
static uint32_t s_pclk_guard_depth = 0;
static uint32_t s_pclk_current_hz = ARS_LCD_PIXEL_CLOCK_HZ;

// Frame buffer complete event callback function
IRAM_ATTR static bool
rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel,
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
 * using the ESP-IDF RGB LCD driver API.
 * Caller should ensure this runs on the core where ISRs should be pinned (e.g.
 * CPU0).
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
              .pclk_hz =
                  ARS_LCD_PIXEL_CLOCK_HZ, // Pixel clock (Configured in Kconfig)
              .h_res = ARS_LCD_H_RES,     // 1024
              .v_res = ARS_LCD_V_RES,     // 600
              // ST7262 Wavershare 7B Timings (1024x600)
              .hsync_pulse_width = 20,
              .hsync_back_porch = 140,
              .hsync_front_porch = 160,
              .vsync_pulse_width = 3,
              .vsync_back_porch = 12,
              .vsync_front_porch = 12,
              .flags =
                  {
                      .pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG,
                      .hsync_idle_low = BOARD_LCD_HSYNC_IDLE_LOW,
                      .vsync_idle_low = BOARD_LCD_VSYNC_IDLE_LOW,
                      .de_idle_high = BOARD_LCD_DE_IDLE_HIGH,
                  },
          },
      .data_width = ARS_RGB_DATA_WIDTH,   // Data width for RGB signals
      .num_fbs = ARS_LCD_RGB_BUFFER_NUMS, // Number of framebuffers
      .bounce_buffer_size_px =
          ARS_RGB_BOUNCE_BUFFER_SIZE,         // Bounce buffer size in pixels
      .dma_burst_size = 64,                   // DMA burst size
      .hsync_gpio_num = ARS_LCD_IO_RGB_HSYNC, // GPIO for horizontal sync
      .vsync_gpio_num = ARS_LCD_IO_RGB_VSYNC, // GPIO for vertical sync
      .de_gpio_num = ARS_LCD_IO_RGB_DE,       // GPIO for data enable
      .pclk_gpio_num = ARS_LCD_IO_RGB_PCLK,   // GPIO for pixel clock
      .disp_gpio_num = ARS_LCD_IO_RGB_DISP,   // GPIO for display enable
      .data_gpio_nums =
          {
              // GPIOs for RGB data signals
              ARS_LCD_IO_RGB_DATA0,
              ARS_LCD_IO_RGB_DATA1,
              ARS_LCD_IO_RGB_DATA2,
              ARS_LCD_IO_RGB_DATA3,
              ARS_LCD_IO_RGB_DATA4,
              ARS_LCD_IO_RGB_DATA5,
              ARS_LCD_IO_RGB_DATA6,
              ARS_LCD_IO_RGB_DATA7,
              ARS_LCD_IO_RGB_DATA8,
              ARS_LCD_IO_RGB_DATA9,
              ARS_LCD_IO_RGB_DATA10,
              ARS_LCD_IO_RGB_DATA11,
              ARS_LCD_IO_RGB_DATA12,
              ARS_LCD_IO_RGB_DATA13,
              ARS_LCD_IO_RGB_DATA14,
              ARS_LCD_IO_RGB_DATA15,
          },
      .flags =
          {
              .fb_in_psram = 1, // Use PSRAM
          },
  };

  ESP_LOGI(
      TAG,
      "RGB panel config: %dx%d pclk=%dHz data_width=%d fb=%d bounce_lines=%d "
      "polarity[pclk_neg=%d h_idle_low=%d v_idle_low=%d de_idle_high=%d]",
      ARS_LCD_H_RES, ARS_LCD_V_RES, ARS_LCD_PIXEL_CLOCK_HZ, ARS_RGB_DATA_WIDTH,
      ARS_LCD_RGB_BUFFER_NUMS, BOARD_LCD_RGB_BOUNCE_BUFFER_LINES,
      BOARD_LCD_PCLK_ACTIVE_NEG, BOARD_LCD_HSYNC_IDLE_LOW,
      BOARD_LCD_VSYNC_IDLE_LOW, BOARD_LCD_DE_IDLE_HIGH);

  // Memory diagnostics before allocation
  size_t fb_size_each =
      (size_t)ARS_LCD_H_RES * ARS_LCD_V_RES * (ARS_LCD_BIT_PER_PIXEL / 8);
  size_t fb_total = fb_size_each * ARS_LCD_RGB_BUFFER_NUMS;
  size_t bounce_size =
      (size_t)ARS_RGB_BOUNCE_BUFFER_SIZE * (ARS_LCD_BIT_PER_PIXEL / 8);

  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t free_dma =
      heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largest_dma =
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

  ESP_LOGI(TAG, "Memory before RGB panel alloc:");
  ESP_LOGI(TAG, "  PSRAM free: %u KB (largest block: %u KB)",
           (unsigned)(free_psram / 1024), (unsigned)(largest_psram / 1024));
  ESP_LOGI(TAG, "  DMA-SRAM free: %u KB (largest block: %u KB)",
           (unsigned)(free_dma / 1024), (unsigned)(largest_dma / 1024));
  ESP_LOGI(TAG, "  Internal free: %u KB", (unsigned)(free_internal / 1024));
  ESP_LOGI(TAG, "  Need: FB=%u KB x%d (PSRAM), bounce=%u KB (DMA-SRAM)",
           (unsigned)(fb_size_each / 1024), ARS_LCD_RGB_BUFFER_NUMS,
           (unsigned)(bounce_size / 1024));

  if (free_psram < fb_total) {
    ESP_LOGE(TAG,
             "PSRAM insufficient! Need %u KB for %d framebuffers, have %u KB",
             (unsigned)(fb_total / 1024), ARS_LCD_RGB_BUFFER_NUMS,
             (unsigned)(free_psram / 1024));
  }
  if (largest_dma < bounce_size) {
    ESP_LOGW(TAG,
             "DMA largest block (%u KB) < bounce buffer (%u KB); may fail or "
             "use fallback",
             (unsigned)(largest_dma / 1024), (unsigned)(bounce_size / 1024));
  }

  ESP_LOGI(
      TAG,
      "Creating RGB Panel (Ensure this task is on CPU0 for optimal ISR usage)");

  // Try to create panel with progressively smaller bounce buffers on failure
  int bounce_lines = BOARD_LCD_RGB_BOUNCE_BUFFER_LINES;
  const int bounce_fallbacks[] = {BOARD_LCD_RGB_BOUNCE_BUFFER_LINES,
                                  BOARD_LCD_RGB_BOUNCE_BUFFER_LINES / 2, 10, 5,
                                  0};
  esp_err_t err = ESP_ERR_NO_MEM;

  for (size_t i = 0; i < sizeof(bounce_fallbacks) / sizeof(bounce_fallbacks[0]);
       i++) {
    bounce_lines = bounce_fallbacks[i];
    panel_config.bounce_buffer_size_px =
        (bounce_lines > 0) ? (ARS_LCD_H_RES * bounce_lines) : 0;

    size_t try_bounce_bytes = (size_t)panel_config.bounce_buffer_size_px *
                              (ARS_LCD_BIT_PER_PIXEL / 8);
    size_t try_largest_dma =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "Trying bounce_lines=%d (px=%u, ~%uKB), DMA largest=%uKB",
             bounce_lines, (unsigned)panel_config.bounce_buffer_size_px,
             (unsigned)(try_bounce_bytes / 1024),
             (unsigned)(try_largest_dma / 1024));

    err = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (err == ESP_OK) {
      if (bounce_lines != BOARD_LCD_RGB_BOUNCE_BUFFER_LINES) {
        ESP_LOGW(
            TAG,
            "RGB panel created with fallback bounce_lines=%d (original=%d)",
            bounce_lines, BOARD_LCD_RGB_BOUNCE_BUFFER_LINES);
      }
      break;
    }

    if (err == ESP_ERR_NO_MEM && bounce_lines > 0) {
      ESP_LOGW(TAG, "Bounce buffer alloc failed (%s), trying smaller size...",
               esp_err_to_name(err));
    } else {
      ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
      break;
    }
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed after all fallbacks: %s",
             esp_err_to_name(err));
    return NULL;
  }

  err = esp_lcd_panel_init(panel_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
    return NULL;
  }

  ESP_LOGI(TAG,
           "RGB panel ready: handle=%p fbs=%d stride_bytes=%d bounce_lines=%d",
           panel_handle, ARS_LCD_RGB_BUFFER_NUMS,
           (int)(ARS_LCD_H_RES * (ARS_LCD_BIT_PER_PIXEL / 8)),
           bounce_lines);
  s_pclk_current_hz = panel_config.timings.pclk_hz;
#if CONFIG_ARS_LCD_PCLK_GUARD_ENABLE
  ESP_LOGI(TAG,
           "PCLK guard: ENABLED base=%u Hz safe=%u Hz settle=%d ms (esp_lcd_rgb_panel_set_pclk)",
           s_pclk_current_hz, CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ,
           CONFIG_ARS_LCD_PCLK_GUARD_SETTLE_MS);
#else
  ESP_LOGI(TAG, "PCLK guard: disabled (CONFIG_ARS_LCD_PCLK_GUARD_ENABLE=n)");
#endif

  // P0-A Diagnostic: Log critical cache configuration for bounce buffer stability
#ifdef CONFIG_ESP32S3_DATA_CACHE_LINE_64B
  ESP_LOGI(TAG, "Cache config: DATA_CACHE_LINE_SIZE=64B (OK for bounce buffer with Octal PSRAM)");
#elif defined(CONFIG_ESP32S3_DATA_CACHE_LINE_32B)
  ESP_LOGW(TAG, "Cache config: DATA_CACHE_LINE_SIZE=32B (SHOULD BE 64B for bounce buffer stability!)");
#else
  ESP_LOGW(TAG, "Cache config: DATA_CACHE_LINE_SIZE=unknown");
#endif

  // Log core affinity for RGB ISR
  ESP_LOGI(TAG, "RGB init on Core %d (ISRs pinned here)", xPortGetCoreID());

  if (ARS_LCD_WAIT_VSYNC_ENABLED) {
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_frame_buf_complete = NULL,
        .on_vsync = rgb_lcd_on_vsync_event, // Dedicated VSYNC notification
    };
    // Callbacks are safe to register from here, the ISR wrapper is already
    // established
    esp_err_t cb_ret =
        esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL);
    if (cb_ret != ESP_OK) {
      ESP_LOGW(TAG,
               "RGB VSYNC callback registration failed: %s (VSYNC sync will be "
               "disabled)",
               esp_err_to_name(cb_ret));
    } else {
      ESP_LOGI(TAG, "RGB VSYNC callback registered successfully");
    }
  } else {
    ESP_LOGI(TAG, "RGB VSYNC callback skipped (CONFIG disabled)");
  }

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

static SemaphoreHandle_t rgb_lcd_pclk_guard_lock_get(void) {
  if (!s_pclk_guard_lock) {
    s_pclk_guard_lock = xSemaphoreCreateMutex();
  }
  return s_pclk_guard_lock;
}

esp_err_t rgb_lcd_port_pclk_guard_enter(const char *reason,
                                        uint32_t *applied_hz) {
#if CONFIG_ARS_LCD_PCLK_GUARD_ENABLE
  (void)reason;
  if (!panel_handle) {
    return ESP_OK;
  }

  SemaphoreHandle_t lock = rgb_lcd_pclk_guard_lock_get();
  if (!lock) {
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = ESP_OK;
  if (s_pclk_guard_depth++ == 0 &&
      CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ > 0 &&
      CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ < s_pclk_current_hz) {
    ret = esp_lcd_rgb_panel_set_pclk(panel_handle,
                                     CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ);
    if (ret == ESP_OK) {
      s_pclk_current_hz = CONFIG_ARS_LCD_PCLK_GUARD_SAFE_HZ;
      if (applied_hz) {
        *applied_hz = s_pclk_current_hz;
      }
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ARS_LCD_PCLK_GUARD_SETTLE_MS));
    } else {
      ESP_LOGW(TAG, "PCLK guard enter failed: %s", esp_err_to_name(ret));
    }
  } else {
    if (applied_hz) {
      *applied_hz = s_pclk_current_hz;
    }
  }

  xSemaphoreGive(lock);
  return ret;
#else
  (void)applied_hz;
  (void)reason;
  return ESP_OK;
#endif
}

esp_err_t rgb_lcd_port_pclk_guard_exit(void) {
#if CONFIG_ARS_LCD_PCLK_GUARD_ENABLE
  if (!panel_handle) {
    return ESP_OK;
  }
  SemaphoreHandle_t lock = rgb_lcd_pclk_guard_lock_get();
  if (!lock) {
    return ESP_ERR_NO_MEM;
  }
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = ESP_OK;
  if (s_pclk_guard_depth > 0) {
    s_pclk_guard_depth--;
  }

  if (s_pclk_guard_depth == 0 &&
      s_pclk_current_hz != ARS_LCD_PIXEL_CLOCK_HZ) {
    ret = esp_lcd_rgb_panel_set_pclk(panel_handle, ARS_LCD_PIXEL_CLOCK_HZ);
    if (ret == ESP_OK) {
      s_pclk_current_hz = ARS_LCD_PIXEL_CLOCK_HZ;
      vTaskDelay(pdMS_TO_TICKS(CONFIG_ARS_LCD_PCLK_GUARD_SETTLE_MS));
    } else {
      ESP_LOGW(TAG, "PCLK guard restore failed: %s", esp_err_to_name(ret));
    }
  }

  xSemaphoreGive(lock);
  return ret;
#else
  return ESP_OK;
#endif
}

esp_err_t rgb_lcd_port_get_framebuffers(void ***buffers, size_t *buffer_count,
                                        size_t *stride_bytes) {
  if (!panel_handle) {
    return ESP_ERR_INVALID_STATE;
  }

  if (s_framebuffer_count == 0) {
    s_stride_bytes = ARS_LCD_H_RES * (ARS_LCD_BIT_PER_PIXEL / 8);
    // Correctly request all framebuffers at once
    // API: esp_lcd_rgb_panel_get_frame_buffer(panel, num_fbs, &fb0, &fb1, ...)
    void *fb0 = NULL;
    void *fb1 = NULL;
    esp_err_t fb_ret = ESP_FAIL;

#if ARS_LCD_RGB_BUFFER_NUMS == 1
    fb_ret = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 1, &fb0);
    if (fb_ret == ESP_OK && fb0) {
      s_framebuffers[0] = fb0;
      s_framebuffer_count = 1;
    }
#elif ARS_LCD_RGB_BUFFER_NUMS == 2
    fb_ret = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb0, &fb1);
    if (fb_ret == ESP_OK && fb0) {
      s_framebuffers[0] = fb0;
      s_framebuffer_count = 1;
      if (fb1) { // fb1 might be NULL if allocation failed partially or
                 // something (unlikely if OK returned)
        s_framebuffers[1] = fb1;
        s_framebuffer_count = 2;
      }
    }
#else
#error "Only 1 or 2 framebuffers supported in this fix"
#endif

    if (fb_ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to get RGB framebuffers: %s",
               esp_err_to_name(fb_ret));
    }

    if (s_framebuffer_count > 0) {
      ESP_LOGI(TAG, "RGB framebuffers ready: %d stride=%d bytes",
               (int)s_framebuffer_count, (int)s_stride_bytes);
      for (size_t i = 0; i < s_framebuffer_count; i++) {
        ESP_LOGI(TAG, "fb[%d]=%p", (int)i, s_framebuffers[i]);
      }
    }
  }

  if (s_framebuffer_count == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  if (buffers) {
    *buffers = s_framebuffers;
  }
  if (buffer_count) {
    *buffer_count = s_framebuffer_count;
  }
  if (stride_bytes) {
    *stride_bytes = s_stride_bytes;
  }
  return ESP_OK;
}
