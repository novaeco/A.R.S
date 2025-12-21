#include "../include/board.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "board_orientation.h"

// ADC One-Shot & Calibration Includes
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "board";
static inline int rotation_deg(lv_display_rotation_t rot) {
  switch (rot) {
  case LV_DISPLAY_ROTATION_90:
    return 90;
  case LV_DISPLAY_ROTATION_180:
    return 180;
  case LV_DISPLAY_ROTATION_270:
    return 270;
  case LV_DISPLAY_ROTATION_0:
  default:
    return 0;
  }
}

// Global Hardware Handles
static esp_lcd_touch_handle_t g_tp_handle = NULL;
static esp_lcd_panel_handle_t g_panel_handle = NULL;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_adc_cali_enabled = false;
static float s_bat_divider = 1.0f;

// BSP Components
#include "gt911.h"
#include "i2c_bus_shared.h" // Added for shared bus init
// #include "lvgl_port.h" // Removed to break circular dependency
#include "rgb_lcd_port.h"
#include "sd.h"
#include "touch.h"
#include "touch_orient.h"
#include "touch_transform.h"

// ... (other includes)

esp_err_t app_board_init(void) {
  ESP_LOGI(TAG, "Initializing Board via BSP...");

  esp_err_t err = i2c_bus_shared_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
    return err;
  }

#if CONFIG_ARS_TOUCH_CLEAR_PROFILES_ON_BOOT
  esp_err_t orient_reset = touch_orient_clear();
  if (orient_reset == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "touch_orient NVS already clean");
  } else if (orient_reset != ESP_OK) {
    ESP_LOGW(TAG, "touch_orient clear failed: %s", esp_err_to_name(orient_reset));
  }

  esp_err_t tf_reset = touch_transform_storage_clear();
  if (tf_reset == ESP_OK) {
    ESP_LOGI(TAG, "touchcal slots cleared at boot");
  } else {
    ESP_LOGW(TAG, "touchcal clear failed: %s", esp_err_to_name(tf_reset));
  }
#endif

  bool ioext_ok = false;
  err = IO_EXTENSION_Init(); // IO Expander (CH32V003, Waveshare)
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IO expander init failed: %s", esp_err_to_name(err));
  } else {
    ioext_ok = true;
  }

  if (ioext_ok) {
    vTaskDelay(pdMS_TO_TICKS(50)); // Allow IO extender to settle
  }

  // 3. Initialize Touch (GT911)
  // Note: touch_gt911_init internally re-checks bus/io but relies on shared
  // instances
  touch_orient_config_t orient_cfg = {0};
  bool orient_cfg_ready = false;

  if (ioext_ok) {
    g_tp_handle = touch_gt911_init();
    if (g_tp_handle == NULL) {
      ESP_LOGE(TAG, "Failed to locate GT911/I2C - Check connections!");
    } else {
      board_orientation_t orient_defaults;
      board_orientation_get_defaults(&orient_defaults);

      esp_err_t orient_err = touch_orient_load(&orient_cfg);
      if (orient_err != ESP_OK) {
        ESP_LOGW(TAG, "touch_orient load failed: %s; using defaults",
                 esp_err_to_name(orient_err));
        touch_orient_get_defaults(&orient_cfg);
        board_orientation_apply_touch_defaults(&orient_cfg, &orient_defaults);
      }

      orient_err = touch_orient_apply(g_tp_handle, &orient_cfg);
      if (orient_err != ESP_OK) {
        ESP_LOGE(TAG, "touch_orient apply failed: %s",
                 esp_err_to_name(orient_err));
      }
      ESP_LOGI(TAG,
               "Touch orientation effective: rot=%d swap=%d mirX=%d mirY=%d",
               rotation_deg(orient_defaults.rotation), orient_cfg.swap_xy,
               orient_cfg.mirror_x, orient_cfg.mirror_y);
      orient_cfg_ready = true;
    }
  } else {
    ESP_LOGW(TAG, "Touch init skipped: IO expander unavailable");
  }

  // 2. CRITICAL: Enable LCD Power (VCOM / LCD_VDD) via IO Expander (IO_6)
  // This MUST happen before LCD Init, or pixels won't drive.
  // 2. CRITICAL: Enable LCD Power (VCOM / LCD_VDD) via IO Expander (IO_6)
  // This MUST happen before LCD Init, or pixels won't drive.
  if (ioext_ok) {
    IO_EXTENSION_Output(IO_EXTENSION_IO_6, 1); // VCOM Enable
    ESP_LOGI(TAG, "LCD VCOM/VDD Enabled (IO 6)");
  } else {
    ESP_LOGE(TAG, "LCD power enable skipped (IO expander offline)");
    return ESP_ERR_INVALID_STATE;
  }

  // Deterministic LCD reset (IO_3) to avoid stuck-black screen after power
  // sequencing. Active-low pulse with short settle times.
  if (ioext_ok) {
    esp_err_t rst_ret = IO_EXTENSION_Output(IO_EXTENSION_IO_3, 0);
    if (rst_ret != ESP_OK) {
      ESP_LOGE(TAG, "LCD reset assert failed: %s", esp_err_to_name(rst_ret));
      return rst_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    rst_ret = IO_EXTENSION_Output(IO_EXTENSION_IO_3, 1);
    if (rst_ret != ESP_OK) {
      ESP_LOGE(TAG, "LCD reset release failed: %s", esp_err_to_name(rst_ret));
      return rst_ret;
    }
    ESP_LOGI(TAG, "LCD reset pulse completed (IO 3)");
  }

  // Wait for power to stabilize
  vTaskDelay(pdMS_TO_TICKS(50));

  // 3. Initialize RGB LCD
  g_panel_handle = waveshare_esp32_s3_rgb_lcd_init();
  if (g_panel_handle == NULL) {
    ESP_LOGE(TAG, "Failed to init RGB LCD");
    return ESP_FAIL;
  }

  // Ensure display is ON
  // Note: esp_lcd_panel_disp_on_off is not supported by RGB panel driver
  // (returns ESP_ERR_NOT_SUPPORTED) The display is enabled by default or via
  // the DISP GPIO handling in the driver.
  ESP_LOGI(TAG,
           "Skipping esp_lcd_panel_disp_on_off (not supported for RGB panel)");

  // 4. Backlight Enable (IO_2)
  // Enable Backlight to see the test pattern
  vTaskDelay(pdMS_TO_TICKS(50));

  esp_err_t bl_ret = ESP_ERR_INVALID_STATE;
#if CONFIG_ARS_BACKLIGHT_USE_IO_EXPANDER
  if (ioext_ok) {
    bl_ret = IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);
    if (bl_ret == ESP_OK) {
      ESP_LOGI(TAG, "Backlight Enabled (IO 2)");
    } else {
      ESP_LOGW(TAG, "Backlight via IO expander failed: %s",
               esp_err_to_name(bl_ret));
    }
  }
#endif

  // Debug: Run Test Pattern (Color Bars) to verify display independently of
  // LVGL
  board_lcd_test_pattern();
  ESP_LOGI(TAG, "LCD pipeline check: VCOM/reset/backlight/test pattern done");

  // 5. Touch Transform (load + migrate legacy if needed)
  if (g_tp_handle) {
    touch_transform_record_t rec = {0};
    esp_err_t err = touch_transform_storage_load(&rec);
    if (err == ESP_ERR_NOT_FOUND) {
      if (touch_transform_storage_migrate_old(&rec) == ESP_OK) {
        err = ESP_OK;
      }
    }
    if (err == ESP_OK) {
      if (touch_transform_validate(&rec.transform) == ESP_OK) {
        ESP_LOGI(TAG,
                 "Touch transform present (gen=%" PRIu32
                 ") swap=%d mirX=%d mirY=%d applying",
                 rec.generation, rec.transform.swap_xy, rec.transform.mirror_x,
                 rec.transform.mirror_y);
        touch_transform_t apply_tf = rec.transform;
        bool has_orient_flags = apply_tf.swap_xy || apply_tf.mirror_x ||
                                apply_tf.mirror_y;
        if (has_orient_flags && orient_cfg_ready) {
          orient_cfg.swap_xy = apply_tf.swap_xy;
          orient_cfg.mirror_x = apply_tf.mirror_x;
          orient_cfg.mirror_y = apply_tf.mirror_y;
          touch_orient_save(&orient_cfg);
          touch_orient_apply(g_tp_handle, &orient_cfg);
        }
        apply_tf.swap_xy = false;
        apply_tf.mirror_x = false;
        apply_tf.mirror_y = false;
        touch_transform_set_active(&apply_tf);
      } else {
        ESP_LOGW(TAG, "Touch transform invalid; using identity");
        touch_transform_t id;
        touch_transform_identity(&id);
        touch_transform_set_active(&id);
      }
    } else {
      if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Touch transform not found in NVS; using defaults");
        if (orient_cfg_ready) {
          board_orientation_t orient_defaults;
          board_orientation_get_defaults(&orient_defaults);
          board_orientation_apply_touch_defaults(&orient_cfg, &orient_defaults);
          touch_orient_apply(g_tp_handle, &orient_cfg);
          touch_orient_save(&orient_cfg);
          ESP_LOGI(TAG,
                   "Defaulted touch orientation from rotation: rot=%d swap=%d "
                   "mirX=%d mirY=%d",
                   rotation_deg(orient_defaults.rotation), orient_cfg.swap_xy,
                   orient_cfg.mirror_x, orient_cfg.mirror_y);
        }
      } else {
        ESP_LOGW(TAG, "Touch transform missing: %s", esp_err_to_name(err));
      }
      touch_transform_t id;
      touch_transform_identity(&id);
      touch_transform_set_active(&id);
    }
  }

#if CONFIG_ARS_BACKLIGHT_GPIO >= 0
  if (bl_ret != ESP_OK) {
    if (ensure_ledc_backlight() == ESP_OK) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      bl_ret = ESP_OK;
      ESP_LOGI(TAG, "Backlight driven by GPIO %d (LEDC)",
               CONFIG_ARS_BACKLIGHT_GPIO);
    } else {
      ESP_LOGW(TAG, "Failed to configure LEDC backlight fallback");
    }
  }
#endif

  if (bl_ret != ESP_OK) {
    ESP_LOGW(TAG, "Backlight not enabled; display may remain dark");
  }

  // ARS: Wait for IO Extender to process and bus to settle before SD Card Init
  vTaskDelay(pdMS_TO_TICKS(100));

  // 3.1 Init Battery ADC
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = BOARD_BAT_ADC_UNIT,
  };
  err = adc_oneshot_new_unit(&init_config1, &s_adc_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ADC init skipped: %s", esp_err_to_name(err));
  } else {
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, BOARD_BAT_ADC_CHAN, &config);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
    }
  }

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t cali_config = {
      .unit_id = BOARD_BAT_ADC_UNIT,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  if (s_adc_handle) {
    if (adc_cali_create_scheme_line_fitting(&cali_config, &s_adc_cali_handle) ==
        ESP_OK) {
      s_adc_cali_enabled = true;
      ESP_LOGI(TAG, "Battery ADC calibrated using line fitting scheme");
    } else {
      ESP_LOGW(TAG, "Battery ADC calibration not available; using raw values");
    }
  }
#endif

  // 4. Initialize SD Card -> MOVED to app_board_init_sd() to avoid blocking UI

  // 5. Initialize LVGL Port
  // MOVED to main.c to break circular dependency (board <-> lvgl_port)
  // ESP_LOGI(TAG, "Starting LVGL Port Init...");
  // if (lvgl_port_init(g_panel_handle, g_tp_handle) != ESP_OK) { ... }

  float div_den = (BOARD_BAT_DIV_DEN > 0) ? (float)BOARD_BAT_DIV_DEN : 1.0f;
  s_bat_divider = (float)BOARD_BAT_DIV_NUM / div_den;
  if (s_bat_divider < 0.1f) {
    s_bat_divider = 1.0f;
  }

  ESP_LOGI(TAG, "Board Initialization Complete (SD pending) (bat_div=%.3f)",
           s_bat_divider);
  return ESP_OK;
}

esp_err_t app_board_init_sd(void) {
  // 4. Initialize SD Card
  // Note: sd_card_init handles mounting to /data/sd or similar defined in sd.c
  ESP_LOGI(TAG, "Starting SD Card Init...");
  if (sd_card_init() != ESP_OK) {
    ESP_LOGW(TAG, "Failed to init SD Card");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "SD Card Init OK");
  return ESP_OK;
}

lv_display_t *app_board_get_disp(void) {
  // Return default display from LVGL
  return lv_display_get_default();
}

lv_indev_t *app_board_get_indev(void) {
  return lv_indev_get_next(NULL);
}

esp_lcd_touch_handle_t app_board_get_touch_handle(void) { return g_tp_handle; }
esp_lcd_panel_handle_t app_board_get_panel_handle(void) {
  return g_panel_handle;
}

static void touch_diagnostic_task(void *arg) {
  ESP_LOGI(TAG, "Starting Touch Diagnostics (10s)...");
  if (!g_tp_handle) {
    ESP_LOGE(TAG, "Touch handle is NULL, skipping test.");
    vTaskDelete(NULL);
  }

  uint16_t x[5], y[5], str[5];
  uint8_t count = 0;

  for (int i = 0; i < 100; i++) {
    esp_lcd_touch_read_data(g_tp_handle);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    bool pressed =
        esp_lcd_touch_get_coordinates(g_tp_handle, x, y, str, &count, 5);
#pragma GCC diagnostic pop
    if (pressed && count > 0) {
      ESP_LOGI(TAG, "Touch: Count=%d, X=%d, Y=%d", count, x[0], y[0]);
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz sample
  }
  ESP_LOGI(TAG, "Touch Diagnostics Complete.");
  vTaskDelete(NULL);
}

void app_board_run_diagnostics(void) {
  xTaskCreate(touch_diagnostic_task, "diag_task", 4096, NULL, 5, NULL);
}

bool board_sd_is_mounted(void) { return (card != NULL); }

void board_set_backlight_percent(uint8_t percent) {
  if (percent > 100)
    percent = 100;

  esp_err_t ret = ESP_ERR_INVALID_STATE;

#if CONFIG_ARS_BACKLIGHT_USE_IO_EXPANDER
  if (IO_EXTENSION_Is_Initialized()) {
    ret = IO_EXTENSION_Pwm_Output(percent);
  }
#endif

#if CONFIG_ARS_BACKLIGHT_GPIO >= 0
  if (ret != ESP_OK && ensure_ledc_backlight() == ESP_OK) {
    uint32_t duty = (1023 * percent) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ret = ESP_OK;
  }
#endif

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Backlight update skipped (no driver available)");
  } else {
    ESP_LOGD(TAG, "Set Backlight: %d%%", percent);
  }
}

esp_err_t board_get_battery_level(uint8_t *percent, uint16_t *voltage_mv) {
  if (!s_adc_handle)
    return ESP_FAIL;
  int adc_raw;
  if (adc_oneshot_read(s_adc_handle, BOARD_BAT_ADC_CHAN, &adc_raw) == ESP_OK) {
    int voltage_mv_raw = 0;
    if (s_adc_cali_enabled &&
        adc_cali_raw_to_voltage(s_adc_cali_handle, adc_raw, &voltage_mv_raw) ==
            ESP_OK) {
      // calibrated value in mV
    } else {
      // Approximate using reference 1100mV * attenuation factor (~3.9x for
      // 12dB)
      voltage_mv_raw = (adc_raw * 1100 * 4) / 4095;
    }

    float divider = (s_bat_divider > 0.1f) ? s_bat_divider : 1.0f;
    uint32_t voltage = (uint32_t)(voltage_mv_raw * divider);
    if (voltage_mv)
      *voltage_mv = (uint16_t)voltage;

    if (percent) {
      const uint32_t vmax = 4200;
      const uint32_t vmin = 3300;
      if (voltage >= vmax)
        *percent = 100;
      else if (voltage <= vmin)
        *percent = 0;
      else
        *percent = (uint8_t)((voltage - vmin) * 100 / (vmax - vmin));
    }
    return ESP_OK;
  }
  return ESP_FAIL;
}

static void board_lcd_draw_test_pattern(void) {
  ESP_LOGI(TAG, "Running Direct LCD Test Pattern...");

  // 1. Allocate buffer (Try PSRAM)
  size_t frame_size = 1024 * 600 * sizeof(uint16_t);
  uint16_t *frame_buf = heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM);

  if (!frame_buf) {
    ESP_LOGE(TAG, "Test pattern: Failed to allocate framebuffer");
    return;
  }

  // 2. Fill buffer with RGBWB bands
  uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
  int bands = 5;
  int band_width = 1024 / bands;

  for (int b = 0; b < bands; b++) {
    uint16_t color = colors[b];
    int x_start = b * band_width;
    int x_end = (b == bands - 1) ? 1024 : (x_start + band_width);

    for (int y = 0; y < 600; y++) {
      for (int x = x_start; x < x_end; x++) {
        frame_buf[y * 1024 + x] = color;
      }
    }
  }

  ESP_LOGI(TAG, "Drawing Test Pattern...");
  esp_lcd_panel_draw_bitmap(g_panel_handle, 0, 0, 1024, 600, frame_buf);
  free(frame_buf);
  ESP_LOGI(TAG, "Test Pattern queued. You should see RGBWB bands.");
}

#if CONFIG_ARS_LCD_BOOT_TEST_PATTERN
static void board_lcd_test_pattern_task(void *arg) {
  if (g_panel_handle) {
    board_lcd_draw_test_pattern();
  }
  vTaskDelay(pdMS_TO_TICKS(CONFIG_ARS_LCD_BOOT_TEST_PATTERN_MS));
  ESP_LOGI(TAG, "Test Pattern window elapsed (%d ms)",
           CONFIG_ARS_LCD_BOOT_TEST_PATTERN_MS);
  vTaskDelete(NULL);
}
#endif

// 3. Flattened Test Pattern (Optimized)
void board_lcd_test_pattern(void) {
  if (!g_panel_handle) {
    ESP_LOGE(TAG, "Test Pattern skipped: Panel handle is NULL");
    return;
  }

#if CONFIG_ARS_SKIP_TEST_PATTERN
  ESP_LOGI(TAG, "Skipping Test Pattern (CONFIG_ARS_SKIP_TEST_PATTERN)");
  return;
#endif

#if CONFIG_ARS_LCD_BOOT_TEST_PATTERN
  if (xTaskCreate(board_lcd_test_pattern_task, "lcd_test", 4096, NULL, 5,
                  NULL) != pdPASS) {
    ESP_LOGW(TAG, "Test pattern task creation failed, running inline");
    board_lcd_draw_test_pattern();
  }
#else
  board_lcd_draw_test_pattern();
#endif
}
