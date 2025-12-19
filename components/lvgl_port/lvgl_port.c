#include "lvgl_port.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "gt911.h"
#include "touch.h"
#include "touch_transform.h"
#include <inttypes.h>

static const char *TAG = "lv_port";          // Tag for logging
static SemaphoreHandle_t lvgl_mux = NULL;    // LVGL mutex for synchronization
static TaskHandle_t lvgl_task_handle = NULL; // Handle for the LVGL task
#if CONFIG_ARS_LVGL_DEBUG_SCREEN
static lv_obj_t *s_debug_screen = NULL; // Debug screen object
#endif

typedef void (*lvgl_port_ui_init_cb_t)(void);

// UI init callback registered by the UI component (set from main before
// lvgl_port_init). We avoid a hard component dependency cycle by storing a
// function pointer instead of including ui.h here.
static lvgl_port_ui_init_cb_t s_ui_init_cb = NULL;

// --- Debug Helper ---
#if CONFIG_ARS_LVGL_DEBUG_SCREEN
static void lv_port_create_debug_screen(void) {
  // Create a dedicated screen object (no parent)
  s_debug_screen = lv_obj_create(NULL);

  // Dark Grey Background
  lv_obj_set_style_bg_color(s_debug_screen, lv_color_hex(0x202020),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_debug_screen, LV_OPA_COVER, LV_PART_MAIN);

  // Debug Label
  lv_obj_t *label = lv_label_create(s_debug_screen);
  lv_label_set_text(label, "LVGL Port OK - AntigravitFix");
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_center(label);

  // Load the screen
  lv_disp_load_scr(s_debug_screen);
  ESP_LOGI(TAG, "Debug screen created and loaded");
}
#endif

// --- 0. Helper: Safe Time Management ---
static uint32_t lvgl_tick_period_ms =
    2; // Default 2ms, adjusted by refresh rate

static void tick_increment(void *arg) {
  /* Tell LVGL how many milliseconds have elapsed */
  lv_tick_inc(lvgl_tick_period_ms);
}

static esp_err_t tick_init(void) {
  // Calculate tick period based on refresh HZ target (approx)
  // If 30Hz -> ~33ms. But we want ticks to be finer than refresh.
  // Standard LVGL usage is fine with 2-5ms ticks.
  lvgl_tick_period_ms = 2;

  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &tick_increment, .name = "LVGL tick"};
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  return esp_timer_start_periodic(lvgl_tick_timer, lvgl_tick_period_ms * 1000);
}

// --- 1. Task Implementation (Fix B1) ---
static void lvgl_port_task(void *arg) {
  ESP_LOGI(TAG, "Starting LVGL task (Core %d, Prio %d)",
           CONFIG_ARS_LVGL_TASK_CORE, CONFIG_ARS_LVGL_TASK_PRIO);

  // Initial delay to let hardware stabilize
  vTaskDelay(pdMS_TO_TICKS(50));

  // Create UI elements in this task contexts (Core 1)
  if (lvgl_port_lock(-1)) {
    // ARS: Always call real UI Init if registered; otherwise log a warning to
    // avoid a hard dependency loop at link time.
    if (s_ui_init_cb) {
      s_ui_init_cb();
    } else {
      ESP_LOGW(TAG, "ui_init() not linked; UI will not start");
    }
    lvgl_port_unlock();
  }

  while (1) {
    if (lvgl_port_lock(-1)) {
      lv_timer_handler();
      lvgl_port_unlock();
    }
    // Yield to prevent WDT on this core (though usually it's Core 0 complaining
    // about IDLE0, but being safe)
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// --- 4. Initialization & API ---

typedef struct {
  SemaphoreHandle_t sem;
  uint8_t consecutive_timeouts;
  bool wait_enabled;
  uint32_t events_seen;
} vsync_sync_t;

static vsync_sync_t s_vsync = {.sem = NULL, .consecutive_timeouts = 0,
                               .wait_enabled = true, .events_seen = 0};

void lvgl_port_set_ui_init_cb(lvgl_port_ui_init_cb_t cb) {
  s_ui_init_cb = cb;
}

bool lvgl_port_notify_rgb_vsync(void) {
  BaseType_t high_task_awoken = pdFALSE;
  if (s_vsync.sem) {
    s_vsync.events_seen++;
    if (!s_vsync.wait_enabled && s_vsync.events_seen > 0) {
      s_vsync.wait_enabled = true;
      s_vsync.consecutive_timeouts = 0;
    }
    xSemaphoreGiveFromISR(s_vsync.sem, &high_task_awoken);
  }
  return high_task_awoken == pdTRUE;
}

static void flush_callback(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;

  // Draw bitmap to LCD panel (PSRAM buffer copy or DMA setup)
  // Debug Trace:
  static bool s_first_flush = true;
  if (s_first_flush) {
    ESP_LOGI(TAG, "LVGL First Flush: (%d,%d) -> (%d,%d)", offsetx1, offsety1,
             offsetx2, offsety2);
    s_first_flush = false;
  }
  // ESP_LOGD(TAG, "Flush: (%d,%d) -> (%d,%d)", offsetx1, offsety1, offsetx2,
  // offsety2);

  esp_err_t draw_ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1,
                                                offsety1, offsetx2 + 1,
                                                offsety2 + 1, px_map);

  if (draw_ret != ESP_OK) {
    ESP_LOGE(TAG, "panel_draw_bitmap failed: %s", esp_err_to_name(draw_ret));
    lv_display_flush_ready(disp);
    return;
  }

  // VSYNC Handshake: Wait for next VSYNC to ensure we don't tear.
  // We only wait if we are large enough to care, or always if strict VSYNC is
  // desired. Timeout is small (20ms) to ensure we don't hang if VSYNC callback
  // fails.
  if (s_vsync.sem && s_vsync.wait_enabled && s_vsync.events_seen > 0) {
    xSemaphoreTake(s_vsync.sem, 0);

    if (xSemaphoreTake(s_vsync.sem, pdMS_TO_TICKS(100)) != pdTRUE) {
      s_vsync.consecutive_timeouts++;
      ESP_LOGW(TAG,
               "VSYNC wait timeout (%u) — check RGB callbacks or PCLK timing",
               s_vsync.consecutive_timeouts);
      if (s_vsync.consecutive_timeouts >= 3) {
        ESP_LOGW(TAG, "VSYNC wait temporarily disabled after repeated timeouts");
        s_vsync.wait_enabled = false;
      }
    } else {
      s_vsync.consecutive_timeouts = 0;
    }
  }

  // Notify LVGL we are done
  lv_display_flush_ready(disp);
}

static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle) {
  assert(panel_handle);
  ESP_LOGI(TAG, "Initializing LVGL Display Driver...");

  // ARS: VSYNC Synchronization Semaphore
  s_vsync.sem = xSemaphoreCreateBinary();
  s_vsync.consecutive_timeouts = 0;
  s_vsync.wait_enabled = true;
  s_vsync.events_seen = 0;

  // Buffer Configuration
  int width = LVGL_PORT_H_RES;
  int lines = CONFIG_ARS_LVGL_BUF_LINES;
  int lines_fallback = (lines / 2 > 10) ? lines / 2 : 10;
  int bpp = CONFIG_ARS_LVGL_COLOR_DEPTH;
  int byte_per_pixel = bpp / 8;

  size_t buffer_size = width * lines * byte_per_pixel;

  ESP_LOGI(TAG, "Allocating LVGL Buffers: %d lines (~%d KB)", lines,
           buffer_size / 1024);

  uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
#if CONFIG_SPIRAM
  caps |= MALLOC_CAP_SPIRAM;
#endif

  void *buf1 = heap_caps_aligned_alloc(64, buffer_size, caps);
  void *buf2 = NULL;

#if CONFIG_ARS_LVGL_USE_DOUBLE_BUF
  buf2 = heap_caps_aligned_alloc(64, buffer_size, caps);
#endif

  if (!buf1 || (CONFIG_ARS_LVGL_USE_DOUBLE_BUF && !buf2)) {
    ESP_LOGW(TAG, "Allocation failed for %d lines, trying fallback %d lines",
             lines, lines_fallback);
    if (buf1) {
      heap_caps_free(buf1);
    }
    if (buf2) {
      heap_caps_free(buf2);
    }

    lines = lines_fallback;
    buffer_size = width * lines * byte_per_pixel;

    buf1 = heap_caps_aligned_alloc(64, buffer_size, caps);
#if CONFIG_ARS_LVGL_USE_DOUBLE_BUF
    buf2 = heap_caps_aligned_alloc(64, buffer_size, caps);
#endif
  }

  if (!buf1) {
    ESP_LOGE(TAG,
             "Critical: Failed to allocate LVGL buffer even with fallback.");
    return NULL;
  }

#if CONFIG_ARS_LVGL_USE_DOUBLE_BUF
  if (!buf2) {
    ESP_LOGW(TAG, "Double buffer disabled dynamically (allocation failed).");
  }
#endif

  lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
  if (!disp) {
    ESP_LOGE(TAG, "Failed to create LVGL display instance");
    heap_caps_free(buf1);
    if (buf2) {
      heap_caps_free(buf2);
    }
    return NULL;
  }

  lv_display_set_flush_cb(disp, flush_callback);
  lv_display_set_buffers(disp, buf1, buf2, buffer_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(disp, panel_handle);

  return disp;
}

// --- 3. Input Device ---
// --- 3. Input Device ---

static int64_t s_last_touch_diag_us = 0;
static uint32_t s_touch_event_seq = 0;

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  esp_lcd_touch_handle_t tp =
      (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
  if (!tp)
    return;

  uint8_t touchpad_cnt = 0;
  esp_lcd_touch_point_data_t touch_points[1] = {0};
  static int16_t last_x = -1;
  static int16_t last_y = -1;
  static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
  static int64_t last_press_us = 0;
  static bool has_valid_point = false;

  // Get coords from last read (buffered in driver data)
  bool pressed = esp_lcd_touch_get_data(tp, touch_points, &touchpad_cnt, 1);

  touch_sample_raw_t raw_sample =
      touch_transform_sample_raw_oriented(tp, false /* orientation */);
  int16_t raw_x = raw_sample.raw_x;
  int16_t raw_y = raw_sample.raw_y;
  // ARS: Apply Calibration
  lv_indev_state_t prev_state = last_state;

  int64_t now_us = esp_timer_get_time();
  bool stable_pressed = pressed && touchpad_cnt > 0;

  if (stable_pressed) {
    if (raw_sample.pressed) {
      raw_x = raw_sample.raw_x;
      raw_y = raw_sample.raw_y;
    } else {
      raw_x = touch_points[0].x;
      raw_y = touch_points[0].y;
    }
    last_press_us = now_us;
    has_valid_point = true;
  }

  // Debounce release for short gaps
  if (!stable_pressed && last_state == LV_INDEV_STATE_PRESSED &&
      (now_us - last_press_us) < 100000) {
    stable_pressed = true;
    touch_points[0].x = last_x;
    touch_points[0].y = last_y;
    raw_x = last_x;
    raw_y = last_y;
  }

  if (stable_pressed) {
    lv_point_t mapped = {.x = raw_x, .y = raw_y};
    const touch_transform_t *tf = touch_transform_get_active();
    if (tf) {
      if (touch_transform_apply(tf, raw_x, raw_y, LVGL_PORT_H_RES,
                                LVGL_PORT_V_RES, &mapped) != ESP_OK) {
        mapped.x = raw_x;
        mapped.y = raw_y;
      }
    }
    int16_t x = mapped.x;
    int16_t y = mapped.y;

    if ((s_touch_event_seq++ % 64) == 0) {
      ESP_LOGD(TAG,
               "touch raw(%d,%d)->(%d,%d) pressed=%d swap=%d mirX=%d mirY=%d",
               raw_x, raw_y, x, y, (int)stable_pressed,
               tf ? tf->swap_xy : -1, tf ? tf->mirror_x : -1,
               tf ? tf->mirror_y : -1);
    }

    // ARS: Jitter Filter
    // 1. Threshold check: ignore small moves if previously pressed
    if (last_state == LV_INDEV_STATE_PRESSED) {
      int diff = abs(x - last_x) + abs(y - last_y);
      if (diff < 5) { // 5px jitter threshold
        x = last_x;
        y = last_y;
      } else {
        // 2. Simple EMA (Exponential Moving Average) for smoothness
        // new = alpha*current + (1-alpha)*old. Alpha ~0.5
        x = (x + last_x) / 2;
        y = (y + last_y) / 2;
      }
    }

    // Clamp to display bounds to avoid LVGL out-of-range
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
      lv_coord_t max_x = lv_display_get_horizontal_resolution(disp) - 1;
      lv_coord_t max_y = lv_display_get_vertical_resolution(disp) - 1;
      if (x < 0)
        x = 0;
      if (y < 0)
        y = 0;
      if (x > max_x)
        x = max_x;
      if (y > max_y)
        y = max_y;
    }

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    last_x = x;
    last_y = y;
    last_state = LV_INDEV_STATE_PRESSED;
  } else {
    if (last_state == LV_INDEV_STATE_PRESSED && has_valid_point) {
      data->state = LV_INDEV_STATE_RELEASED;
      data->point.x = last_x;
      data->point.y = last_y;
      last_state = LV_INDEV_STATE_RELEASED;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
      data->point.x = has_valid_point ? last_x : 0;
      data->point.y = has_valid_point ? last_y : 0;
    }
  }

  bool state_changed = data->state != prev_state;

  static uint32_t s_last_diag_invalid = 0;
  static uint32_t s_last_diag_clamped = 0;
  static int64_t s_last_diag_log_us = 0;
  gt911_stats_t stats = {0};
  gt911_get_stats(&stats);

  if ((now_us - s_last_diag_log_us) >= 500000 &&
      (stats.invalid_points != s_last_diag_invalid ||
       stats.clamped_points != s_last_diag_clamped)) {
    s_last_diag_log_us = now_us;
    if (stats.invalid_points != s_last_diag_invalid) {
      ESP_LOGW("TOUCH_EVT",
               "dropped=%" PRIu32 " last_invalid=(%u,%u) total_irqs=%" PRIu32,
               stats.invalid_points - s_last_diag_invalid, stats.last_invalid_x,
               stats.last_invalid_y, stats.irq_total);
      s_last_diag_invalid = stats.invalid_points;
    }
    if (stats.clamped_points != s_last_diag_clamped) {
      ESP_LOGI("TOUCH_EVT",
               "clamped=%" PRIu32 " last_raw=(%u,%u) last_xy=(%d,%d)",
               stats.clamped_points - s_last_diag_clamped,
               stats.last_clamped_x, stats.last_clamped_y, data->point.x,
               data->point.y);
      s_last_diag_clamped = stats.clamped_points;
    }
  }

#if CONFIG_TOUCH_DEBUG_LOG
  bool rate_allow =
      data->state == LV_INDEV_STATE_PRESSED &&
      ((now_us - s_last_touch_diag_us) >= 1000000);
  bool log_release = state_changed && prev_state == LV_INDEV_STATE_PRESSED;
  if (state_changed || rate_allow || log_release) {
    s_last_touch_diag_us = now_us;
    s_touch_event_seq++;

    const char *state_str =
        data->state == LV_INDEV_STATE_PRESSED ? "pressed" : "released";
    ESP_LOGI("TOUCH_EVT",
             "seq=%" PRIu32 " state=%s x=%d y=%d raw_x=%d raw_y=%d",
             s_touch_event_seq, state_str, data->point.x, data->point.y, raw_x,
             raw_y);
  }
#else
  if (state_changed && data->state == LV_INDEV_STATE_PRESSED) {
    s_last_touch_diag_us = now_us;
    s_touch_event_seq++;
    ESP_LOGI("TOUCH_EVT", "seq=%" PRIu32 " pressed x=%d y=%d raw_x=%d raw_y=%d",
             s_touch_event_seq, data->point.x, data->point.y, raw_x, raw_y);
  } else if (state_changed && data->state == LV_INDEV_STATE_RELEASED &&
             has_valid_point) {
    s_last_touch_diag_us = now_us;
    s_touch_event_seq++;
    ESP_LOGD("TOUCH_EVT", "seq=%" PRIu32 " released x=%d y=%d",
             s_touch_event_seq, data->point.x, data->point.y);
  }
#endif

  ars_touch_debug_feed(raw_x, raw_y, data->point.x, data->point.y,
                       data->state == LV_INDEV_STATE_PRESSED);
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp) {
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchpad_read);
  lv_indev_set_user_data(indev, tp);
  return indev;
}

// --- 4. Initialization & API ---

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                         esp_lcd_touch_handle_t tp_handle) {
  ESP_LOGI(TAG, "Initializing LVGL Port (AntigravitFix)");

  lv_init();
  ESP_ERROR_CHECK(tick_init());

  lv_display_t *disp = display_init(lcd_handle);
  assert(disp);

  if (tp_handle) {
    lv_indev_t *indev = indev_init(tp_handle);
    assert(indev);
  }

  lvgl_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_mux);

  BaseType_t core_id = CONFIG_ARS_LVGL_TASK_CORE;
  if (core_id > 1 || core_id < 0)
    core_id = tskNO_AFFINITY;

  BaseType_t ret = xTaskCreatePinnedToCore(
      lvgl_port_task, "lvgl", CONFIG_ARS_LVGL_TASK_STACK, NULL,
      CONFIG_ARS_LVGL_TASK_PRIO, &lvgl_task_handle, core_id);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    return ESP_FAIL;
  }

  // Debug Screen creation moved to lvgl_port_task to execute on correct core
  // lv_port_create_debug_screen();

  return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms) {
  if (!lvgl_mux)
    return false;
  const TickType_t timeout_ticks =
      (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void) {
  if (!lvgl_mux)
    return;
  xSemaphoreGiveRecursive(lvgl_mux);
}

bool lvgl_port_in_task_context(void) {
  return lvgl_task_handle &&
         (xTaskGetCurrentTaskHandle() == lvgl_task_handle);
}

TaskHandle_t lvgl_port_get_task_handle(void) { return lvgl_task_handle; }

// Dummy for compilation compatibility if needed by external calls,
// though AntigravitFix simplifies the flush logic significantly.

// Fallback implementation removed to force linker error if UI is missing
