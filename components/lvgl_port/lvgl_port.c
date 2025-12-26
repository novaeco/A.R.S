#include "lvgl_port.h"
#include "board_orientation.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gt911.h"
#include "lvgl.h"
#include "rgb_lcd_port.h"
#include "sdkconfig.h"
#include "touch.h"
#include "touch_orient.h"
#include "touch_transform.h"
#include <inttypes.h>

#if defined(CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE) &&                               \
    CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE
#define ARS_LCD_WAIT_VSYNC_ENABLED CONFIG_ARS_LCD_WAIT_VSYNC
#elif defined(CONFIG_ARS_LCD_WAIT_VSYNC)
#define ARS_LCD_WAIT_VSYNC_ENABLED CONFIG_ARS_LCD_WAIT_VSYNC
#else
#define ARS_LCD_WAIT_VSYNC_ENABLED 0
#endif

#if defined(CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS)
#define ARS_LCD_WAIT_VSYNC_TIMEOUT_MS CONFIG_ARS_LCD_WAIT_VSYNC_TIMEOUT_MS
#else
#define ARS_LCD_WAIT_VSYNC_TIMEOUT_MS 40
#endif

static const char *TAG = "lv_port";          // Tag for logging
static SemaphoreHandle_t lvgl_mux = NULL;    // LVGL mutex for synchronization
static TaskHandle_t lvgl_task_handle = NULL; // Handle for the LVGL task
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
#if CONFIG_ARS_UI_HEARTBEAT
static lv_obj_t *s_heartbeat_label = NULL;
static lv_timer_t *s_heartbeat_timer = NULL;
#endif
#if CONFIG_ARS_LVGL_DEBUG_SCREEN
static lv_obj_t *s_debug_screen = NULL; // Debug screen object
#endif

static void **s_rgb_framebuffers = NULL;
static size_t s_rgb_framebuffer_count = 0;
static size_t s_rgb_stride_bytes = 0;

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
  lv_label_set_text(label, "LVGL DIRECT OK");
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_center(label);

  // Load the screen
  lv_disp_load_scr(s_debug_screen);
  ESP_LOGI(TAG, "Debug screen created and loaded");
}
#endif

#if CONFIG_ARS_UI_HEARTBEAT
static void lv_port_heartbeat_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  static uint32_t beat = 0;
  if (s_heartbeat_label) {
    lv_label_set_text_fmt(s_heartbeat_label, "HB %u", (unsigned)beat++);
  }
}

static void lv_port_create_heartbeat(void) {
  if (s_heartbeat_label || !lv_display_get_default()) {
    return;
  }
  s_heartbeat_label = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(s_heartbeat_label, lv_color_hex(0x00FF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(s_heartbeat_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_align(s_heartbeat_label, LV_ALIGN_TOP_LEFT, 4, 4);
  lv_label_set_text(s_heartbeat_label, "HB 0");
  s_heartbeat_timer = lv_timer_create(lv_port_heartbeat_timer_cb, 1000, NULL);
  ESP_LOGI(TAG, "Heartbeat UI enabled");
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
  if (s_lvgl_tick_timer == NULL) {
    esp_err_t err = esp_timer_create(&lvgl_tick_timer_args, &s_lvgl_tick_timer);
    if (err != ESP_OK) {
      return err;
    }
  }

  esp_timer_stop(s_lvgl_tick_timer);
  return esp_timer_start_periodic(s_lvgl_tick_timer,
                                  lvgl_tick_period_ms * 1000);
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
#if CONFIG_ARS_UI_HEARTBEAT
    lv_port_create_heartbeat();
#endif
    lvgl_port_unlock();
  }

  while (1) {
    if (lvgl_port_lock(-1)) {
      lv_timer_handler();
      lvgl_port_unlock();
    }
    // Yield to prevent WDT on CPU1 - CRITICAL for preventing IDLE1 starvation
    // With priority reduced to 5 (from 10), this delay ensures IDLE gets time
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// --- 4. Initialization & API ---

typedef struct {
  TaskHandle_t wait_task;
  SemaphoreHandle_t sem;
  esp_timer_handle_t diag_timer;
  volatile uint32_t isr_count;
  volatile uint32_t wait_wakeups;
  volatile int64_t last_vsync_us;
  volatile int64_t last_period_us;
  int64_t last_log_us;
  uint32_t last_log_count;
  uint32_t wait_log_budget;
  bool wait_enabled;
  bool wait_supported;
  bool timeout_logged;
} vsync_sync_t;

static portMUX_TYPE s_vsync_spinlock = portMUX_INITIALIZER_UNLOCKED;
static vsync_sync_t s_vsync = {.wait_task = NULL,
                               .sem = NULL,
                               .diag_timer = NULL,
                               .isr_count = 0,
                               .wait_wakeups = 0,
                               .last_vsync_us = 0,
                               .last_period_us = 0,
                               .last_log_us = 0,
                               .last_log_count = 0,
                               .wait_log_budget = 3,
                               .wait_enabled = ARS_LCD_WAIT_VSYNC_ENABLED,
                               .wait_supported = ARS_LCD_WAIT_VSYNC_ENABLED,
                               .timeout_logged = false};

void lvgl_port_set_ui_init_cb(lvgl_port_ui_init_cb_t cb) { s_ui_init_cb = cb; }

static void vsync_diag_timer_cb(void *arg) {
  LV_UNUSED(arg);
  uint32_t count = 0;
  uint32_t wakeups = 0;
  int64_t last_us = 0;

  portENTER_CRITICAL(&s_vsync_spinlock);
  count = s_vsync.isr_count;
  wakeups = s_vsync.wait_wakeups;
  last_us = s_vsync.last_vsync_us;
  portEXIT_CRITICAL(&s_vsync_spinlock);

  const int64_t now_us = esp_timer_get_time();
  if (s_vsync.last_log_us == 0) {
    s_vsync.last_log_us = now_us;
    s_vsync.last_log_count = count;
    return;
  }

  const uint32_t delta_count = count - s_vsync.last_log_count;
  const int64_t delta_us = now_us - s_vsync.last_log_us;
  const uint32_t rate_hz =
      (delta_us > 0) ? (uint32_t)((delta_count * 1000000ULL) / delta_us) : 0;

  s_vsync.last_log_us = now_us;
  s_vsync.last_log_count = count;

  ESP_LOGI(TAG,
           "VSYNC diag: total=%" PRIu32 " wakeups=%" PRIu32 " rate=%" PRIu32
           "/s last=%" PRId64 "us wait=%d derived=%" PRId64 "us",
           count, wakeups, rate_hz, last_us, (int)s_vsync.wait_enabled,
           s_vsync.last_period_us);
}

IRAM_ATTR bool lvgl_port_notify_rgb_vsync(void) {
  BaseType_t high_task_awoken = pdFALSE;
  if (s_vsync.wait_supported) {
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_vsync_spinlock);
    if (s_vsync.last_vsync_us > 0) {
      int64_t period = now_us - s_vsync.last_vsync_us;
      if (period > 0) {
        if (s_vsync.last_period_us == 0) {
          s_vsync.last_period_us = period;
        } else {
          s_vsync.last_period_us = (s_vsync.last_period_us * 3 + period) / 4;
        }
      }
    }
    s_vsync.isr_count++;
    s_vsync.wait_wakeups++;
    s_vsync.last_vsync_us = now_us;
    portEXIT_CRITICAL_ISR(&s_vsync_spinlock);

    TaskHandle_t target = s_vsync.wait_task;
    if (target) {
      vTaskNotifyGiveFromISR(target, &high_task_awoken);
    } else if (s_vsync.sem) {
      xSemaphoreGiveFromISR(s_vsync.sem, &high_task_awoken);
    }
  }
  return high_task_awoken == pdTRUE;
}

static int framebuffer_index_for_ptr(const uint8_t *ptr) {
  if (!ptr || !s_rgb_framebuffers)
    return -1;
  for (size_t i = 0; i < s_rgb_framebuffer_count; i++) {
    if (ptr == s_rgb_framebuffers[i]) {
      return (int)i;
    }
  }
  return -1;
}

static bool s_direct_mode = false;

static uint32_t vsync_wait_timeout_ms(void) {
  const uint32_t min_timeout_ms = 50;
  const uint32_t max_timeout_ms = 150;
  uint32_t cfg_timeout = ARS_LCD_WAIT_VSYNC_TIMEOUT_MS;
  int64_t measured_period_us = 0;

  portENTER_CRITICAL(&s_vsync_spinlock);
  measured_period_us = s_vsync.last_period_us;
  portEXIT_CRITICAL(&s_vsync_spinlock);

  uint32_t derived_ms = 0;
  if (measured_period_us > 0) {
    derived_ms = (uint32_t)((measured_period_us * 3) / 2000);
  }

  uint32_t timeout_ms = cfg_timeout;
  if (derived_ms > timeout_ms) {
    timeout_ms = derived_ms;
  }
  if (timeout_ms < min_timeout_ms) {
    timeout_ms = min_timeout_ms;
  }
  if (timeout_ms > max_timeout_ms) {
    timeout_ms = max_timeout_ms;
  }

  return timeout_ms;
}

static void flush_callback(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map) {
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;

  static bool s_first_flush = true;
  const bool first_flush = s_first_flush;
  const int64_t flush_start_us = esp_timer_get_time();
  int fb_idx = framebuffer_index_for_ptr(px_map);
  if (s_first_flush) {
    ESP_LOGI(TAG,
             "LVGL First Flush (%s): area(%d,%d)-(%d,%d) fb_idx=%d fb_cnt=%d",
             s_direct_mode ? "direct" : "fallback", offsetx1, offsety1,
             offsetx2, offsety2, fb_idx, (int)s_rgb_framebuffer_count);
    s_first_flush = false;
  }

  if (s_direct_mode && fb_idx < 0) {
    ESP_LOGW(TAG, "Flush buffer not tracked (addr=%p) while in DIRECT mode",
             px_map);
  }

  if (s_direct_mode) {
    // In DIRECT mode, px_map IS the framebuffer pointer.
    // We must invoke draw_bitmap to trigger the double-buffer swap in the RGB
    // driver. We pass the full screen area because we are swapping the entire
    // buffer.
    esp_err_t ret = esp_lcd_panel_draw_bitmap(
        panel_handle, 0, 0, LVGL_PORT_H_RES, LVGL_PORT_V_RES, px_map);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Direct swap failed: %s", esp_err_to_name(ret));
    }
  } else {
    esp_err_t draw_ret = ESP_OK;
    if (panel_handle) {
      draw_ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1,
                                           offsetx2 + 1, offsety2 + 1, px_map);
      if (draw_ret != ESP_OK) {
        ESP_LOGE(TAG, "panel_draw_bitmap failed: %s",
                 esp_err_to_name(draw_ret));
        lv_display_flush_ready(disp);
        return;
      }
    }
  }

  const bool wait_vsync = s_direct_mode && s_vsync.sem &&
                          s_vsync.wait_supported && s_vsync.wait_enabled;

  // VSYNC Handshake: Wait for next VSYNC to ensure we don't tear. Timeout is
  // small (configurable) to avoid blocking if the callback is missing.
  if (wait_vsync) {
    if (!s_vsync.wait_task) {
      s_vsync.wait_task = xTaskGetCurrentTaskHandle();
    }

    ulTaskNotifyTake(pdTRUE, 0);

    const uint32_t timeout_ms = vsync_wait_timeout_ms();
    const uint32_t notified =
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
    if (notified == 0) {
      uint32_t isr_count = 0;
      int64_t last_us = 0;
      portENTER_CRITICAL(&s_vsync_spinlock);
      isr_count = s_vsync.isr_count;
      last_us = s_vsync.last_vsync_us;
      portEXIT_CRITICAL(&s_vsync_spinlock);

      if (!s_vsync.timeout_logged) {
        ESP_LOGW(TAG, "VSYNC wait timeout — retrying");
        s_vsync.timeout_logged = true;
      }
      ESP_LOGW(TAG,
               "VSYNC wait result: timeout after %dms (isr_count=%" PRIu32
               " last_us=%" PRId64 ")",
               timeout_ms, isr_count, last_us);
    } else {
      const int64_t now_us = esp_timer_get_time();
      uint32_t wakeups = 0;
      portENTER_CRITICAL(&s_vsync_spinlock);
      wakeups = s_vsync.wait_wakeups;
      s_vsync.last_vsync_us = now_us;
      s_vsync.timeout_logged = false;
      portEXIT_CRITICAL(&s_vsync_spinlock);

      if (s_vsync.wait_log_budget) {
        ESP_LOGI(TAG,
                 "VSYNC wait result: notified=%" PRIu32
                 " wakeups=%" PRIu32 " last_us=%" PRId64,
                 notified, wakeups, now_us);
        s_vsync.wait_log_budget--;
      }
    }
  }

  // Notify LVGL we are done
  lv_display_flush_ready(disp);

  const int64_t flush_duration_us = esp_timer_get_time() - flush_start_us;
  if (!first_flush && flush_duration_us > 5000) {
    ESP_LOGW(TAG,
             "Flush duration high: area(%d,%d)-(%d,%d) fb_idx=%d time=%" PRId64
             "us",
             offsetx1, offsety1, offsetx2, offsety2, fb_idx, flush_duration_us);
  }
}

static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle) {
  if (!panel_handle) {
    ESP_LOGE(TAG, "Display init aborted: panel handle NULL");
    return NULL;
  }
  ESP_LOGI(TAG, "Initializing LVGL Display Driver...");

  s_direct_mode = false;

  // ARS: VSYNC Synchronization Semaphore
  s_vsync.wait_enabled = ARS_LCD_WAIT_VSYNC_ENABLED;
  s_vsync.wait_supported = ARS_LCD_WAIT_VSYNC_ENABLED;
  s_vsync.timeout_logged = false;
  s_vsync.isr_count = 0;
  s_vsync.wait_wakeups = 0;
  s_vsync.last_vsync_us = 0;
  s_vsync.last_period_us = 0;
  s_vsync.wait_log_budget = 3;
  s_vsync.wait_task = NULL;

  if (s_vsync.wait_supported) {
    s_vsync.sem = xSemaphoreCreateBinary();
    if (!s_vsync.sem) {
      ESP_LOGE(TAG, "Failed to allocate VSYNC semaphore");
      s_vsync.wait_enabled = false;
      s_vsync.wait_supported = false;
      ESP_LOGI(TAG, "VSYNC sync: DISABLED (semaphore alloc failed)");
    } else {
      s_vsync.wait_enabled = s_vsync.wait_enabled && s_vsync.wait_supported;
      ESP_LOGI(TAG, "VSYNC sync: ACTIVE (timeout=%dms)", ARS_LCD_WAIT_VSYNC_TIMEOUT_MS);
      if (!s_vsync.diag_timer) {
        const esp_timer_create_args_t diag_args = {
            .callback = vsync_diag_timer_cb, .name = "vsync_diag"};
        esp_err_t diag_ret =
            esp_timer_create(&diag_args, &s_vsync.diag_timer);
        if (diag_ret != ESP_OK) {
          ESP_LOGW(TAG, "VSYNC diag timer create failed: %s",
                   esp_err_to_name(diag_ret));
        } else {
          s_vsync.last_log_us = 0;
          s_vsync.last_log_count = 0;
          esp_err_t start_ret =
              esp_timer_start_periodic(s_vsync.diag_timer, 1000000);
          if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "VSYNC diag timer start failed: %s",
                     esp_err_to_name(start_ret));
          }
        }
      }
    }
  } else {
    ESP_LOGI(TAG, "VSYNC sync: DISABLED (CONFIG_ARS_LCD_VSYNC_WAIT_ENABLE=n)");
  }

  esp_err_t fb_ret = rgb_lcd_port_get_framebuffers(
      &s_rgb_framebuffers, &s_rgb_framebuffer_count, &s_rgb_stride_bytes);

  size_t frame_bytes = 0;
  lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
  if (!disp) {
    ESP_LOGE(TAG, "Failed to create LVGL display instance");
    return NULL;
  }

  lv_display_set_flush_cb(disp, flush_callback);
  lv_display_set_user_data(disp, panel_handle);

  if (fb_ret == ESP_OK && s_rgb_framebuffer_count > 0 &&
      s_rgb_framebuffers != NULL) {

    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t bpp = lv_color_format_get_size(cf);
    uint32_t expected_stride = LVGL_PORT_H_RES * bpp;

    if (s_rgb_stride_bytes == expected_stride) {
      frame_bytes = s_rgb_stride_bytes * LVGL_PORT_V_RES;
      lv_display_set_buffers(
          disp, s_rgb_framebuffers[0],
          (s_rgb_framebuffer_count > 1) ? s_rgb_framebuffers[1] : NULL,
          frame_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
      ESP_LOGI(
          TAG,
          "LVGL DIRECT mode ready: fb_count=%d stride=%d bytes frame=%d bytes",
          (int)s_rgb_framebuffer_count, (int)s_rgb_stride_bytes,
          (int)frame_bytes);
      s_direct_mode = true;
      if (s_rgb_framebuffer_count >= 1) {
        ESP_LOGI(TAG, "fb0=%p", s_rgb_framebuffers[0]);
      }
      if (s_rgb_framebuffer_count >= 2) {
        ESP_LOGI(TAG, "fb1=%p", s_rgb_framebuffers[1]);
      }
      return disp;
    } else {
      ESP_LOGW(TAG,
               "Direct mode stride mismatch: driver=%d vs lvgl=%d (w=%d "
               "bpp=%d). Forcing PARTIAL.",
               (int)s_rgb_stride_bytes, (int)expected_stride, LVGL_PORT_H_RES,
               (int)bpp);
    }
  }

  // Fallback: allocate LVGL-managed buffers (partial render) when RGB driver
  // doesn't expose framebuffers (prevents black screen on incompatible builds)
  ESP_LOGW(TAG,
           "RGB framebuffers unavailable (%s); falling back to partial buffers",
           esp_err_to_name(fb_ret));

  const size_t buf_lines = CONFIG_ARS_LVGL_BUF_LINES;
  lv_color_format_t cf = lv_display_get_color_format(disp);
  uint32_t bpp = lv_color_format_get_size(cf);
  frame_bytes = LVGL_PORT_H_RES * buf_lines * bpp;

  bool buf_psram = false;
  void *buf1 =
      heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf1) {
    buf_psram = true;
  } else {
    buf1 = heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT);
  }
  if (!buf1) {
    ESP_LOGE(TAG, "Failed to allocate primary LVGL buffer (%d bytes)",
             (int)frame_bytes);
    return NULL;
  }

  void *buf2 = NULL;
#if CONFIG_ARS_LVGL_USE_DOUBLE_BUF
  bool buf2_psram = false;
  buf2 = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf2) {
    buf2_psram = true;
  } else {
    buf2 = heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT);
  }
  if (!buf2) {
    ESP_LOGW(TAG, "Double buffer allocation failed; using single buffer");
  }
#endif

  lv_display_set_buffers(disp, buf1, buf2, frame_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

#if CONFIG_ARS_LVGL_USE_DOUBLE_BUF
  const char *buf2_desc =
      buf2 ? (buf2_psram ? "buf2=psram" : "buf2=internal") : "buf2=absent";
#else
  const char *buf2_desc = "buf2=disabled";
#endif

  ESP_LOGI(
      TAG,
      "LVGL PARTIAL fallback: lines=%d buf_bytes=%d double=%s (buf1=%s %s)",
      (int)buf_lines, (int)frame_bytes, buf2 ? "yes" : "no",
      buf_psram ? "psram" : "internal", buf2_desc);

  return disp;
}

// --- 3. Input Device ---
// --- 3. Input Device ---

static int64_t s_last_touch_diag_us = 0;
static uint32_t s_touch_event_seq = 0;
static const char *TOUCH_DIAG_TAG = "TOUCH_DIAG";
#if CONFIG_ARS_TOUCH_DIAG
static int64_t s_touch_keepalive_us = 0;
#endif

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  esp_lcd_touch_handle_t tp =
      (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
  if (!tp)
    return;

  int64_t now_us = esp_timer_get_time();
  // Fixed: Do NOT call esp_lcd_touch_read_data here.
  // The GT911 driver handles reading in its own IRQ/Poll task.
  // Calling it here causes I2C contention and timeouts.

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
  bool stable_pressed = pressed && touchpad_cnt > 0;

  lv_point_t oriented_point = {.x = raw_x, .y = raw_y};
  lv_point_t mapped_point = oriented_point;

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
    // Pipeline: raw -> orientation (swap/mirror/clamp) -> calibration -> LVGL
    const bool orient_applied = touch_orient_driver_applied();
    const touch_orient_config_t *orient = touch_orient_get_active();
    lv_point_t oriented = {.x = raw_x, .y = raw_y};
    if (!orient_applied) {
      touch_orient_map_point(orient, raw_x, raw_y, LVGL_PORT_H_RES,
                             LVGL_PORT_V_RES, &oriented);
    }

    oriented_point = oriented;
    mapped_point = oriented;
    const touch_transform_t *tf = touch_transform_get_active();
    if (tf) {
      if (touch_transform_apply_ex(tf, oriented.x, oriented.y, LVGL_PORT_H_RES,
                                   LVGL_PORT_V_RES, false,
                                   &mapped_point) != ESP_OK) {
        mapped_point = oriented;
      }
    }
    int16_t x = mapped_point.x;
    int16_t y = mapped_point.y;

    if ((s_touch_event_seq++ % 64) == 0) {
      ESP_LOGD(TAG, "touch raw(%d,%d)->orient(%d,%d)->final(%d,%d) pressed=%d",
               raw_x, raw_y, (int)oriented_point.x, (int)oriented_point.y, x, y,
               (int)stable_pressed);
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
    if (has_valid_point) {
      oriented_point.x = last_x;
      oriented_point.y = last_y;
    }
  }

  bool state_changed = data->state != prev_state;

  static uint32_t s_last_diag_invalid = 0;
  static uint32_t s_last_diag_clamped = 0;
  static int64_t s_last_diag_log_us = 0;
  static uint32_t s_last_diag_i2c_err = 0;
  gt911_stats_t stats = {0};
  gt911_get_stats(&stats);

  if ((now_us - s_last_diag_log_us) >= 500000 &&
      (stats.invalid_points != s_last_diag_invalid ||
       stats.clamped_points != s_last_diag_clamped ||
       stats.i2c_errors != s_last_diag_i2c_err)) {
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
               stats.clamped_points - s_last_diag_clamped, stats.last_clamped_x,
               stats.last_clamped_y, data->point.x, data->point.y);
      s_last_diag_clamped = stats.clamped_points;
    }
    if (stats.i2c_errors != s_last_diag_i2c_err) {
      ESP_LOGW("TOUCH_EVT",
               "i2c_errors=%" PRIu32 " poll_timeouts=%" PRIu32
               " (delta %" PRIu32 ")",
               stats.i2c_errors, stats.poll_timeouts,
               stats.i2c_errors - s_last_diag_i2c_err);
      s_last_diag_i2c_err = stats.i2c_errors;
    }
  }

  bool log_press = data->state == LV_INDEV_STATE_PRESSED &&
                   ((now_us - s_last_touch_diag_us) >= 200000 || state_changed);
  bool log_release =
      state_changed && prev_state == LV_INDEV_STATE_PRESSED && has_valid_point;
  if (log_press || log_release) {
    s_last_touch_diag_us = now_us;
    s_touch_event_seq++;
    const char *state_str =
        data->state == LV_INDEV_STATE_PRESSED ? "pressed" : "released";
    ESP_LOGI(TOUCH_DIAG_TAG,
             "seq=%" PRIu32 " %s raw=(%d,%d) orient=(%d,%d) final=(%d,%d)",
             s_touch_event_seq, state_str, raw_x, raw_y, (int)oriented_point.x,
             (int)oriented_point.y, data->point.x, data->point.y);
  }

#if CONFIG_ARS_TOUCH_DIAG
  if ((now_us - s_touch_keepalive_us) >= 1000000) {
    s_touch_keepalive_us = now_us;
    const char *state_str =
        data->state == LV_INDEV_STATE_PRESSED ? "pressed" : "released";
    ESP_LOGI(TOUCH_DIAG_TAG, "read_cb alive (%s) last=(%d,%d)", state_str,
             (int)data->point.x, (int)data->point.y);
  }
#endif

  ars_touch_debug_feed(raw_x, raw_y, data->point.x, data->point.y,
                       data->state == LV_INDEV_STATE_PRESSED);
}

static lv_indev_t *indev_init(lv_display_t *disp, esp_lcd_touch_handle_t tp) {
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchpad_read);
  lv_indev_set_user_data(indev, tp);
  if (disp) {
    lv_indev_set_display(indev, disp);
  }
  return indev;
}

// --- 4. Initialization & API ---

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                         esp_lcd_touch_handle_t tp_handle) {
  ESP_LOGI(TAG, "lvgl_port_init enter panel=%p touch=%p", lcd_handle,
           tp_handle);

  lv_init();
  esp_err_t err = tick_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LVGL tick init failed: %s", esp_err_to_name(err));
    return err;
  }

  lv_display_t *disp = display_init(lcd_handle);
  if (!disp) {
    esp_timer_stop(s_lvgl_tick_timer);
    return ESP_FAIL;
  }

  board_orientation_t orient_defaults;
  board_orientation_get_defaults(&orient_defaults);
  board_orientation_apply_display(disp, &orient_defaults);

  if (tp_handle) {
    lv_indev_t *indev = indev_init(disp, tp_handle);
    if (!indev) {
      ESP_LOGE(TAG, "Failed to create LVGL input device");
      esp_timer_stop(s_lvgl_tick_timer);
      return ESP_FAIL;
    }
  }

  lvgl_mux = xSemaphoreCreateRecursiveMutex();
  if (!lvgl_mux) {
    ESP_LOGE(TAG, "Failed to create LVGL mutex");
    esp_timer_stop(s_lvgl_tick_timer);
    return ESP_ERR_NO_MEM;
  }

  BaseType_t core_id = CONFIG_ARS_LVGL_TASK_CORE;
  if (core_id > 1 || core_id < 0)
    core_id = tskNO_AFFINITY;

  BaseType_t ret = xTaskCreatePinnedToCore(
      lvgl_port_task, "lvgl", CONFIG_ARS_LVGL_TASK_STACK, NULL,
      CONFIG_ARS_LVGL_TASK_PRIO, &lvgl_task_handle, core_id);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    vSemaphoreDelete(lvgl_mux);
    esp_timer_stop(s_lvgl_tick_timer);
    return ESP_FAIL;
  }
  s_vsync.wait_task = lvgl_task_handle;

  // Debug Screen creation moved to lvgl_port_task to execute on correct core
  // lv_port_create_debug_screen();

  ESP_LOGI(TAG, "lvgl_port_init done (disp=%p direct=%d fb_count=%d stride=%d)",
           disp, s_direct_mode, (int)s_rgb_framebuffer_count,
           (int)s_rgb_stride_bytes);

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
  return lvgl_task_handle && (xTaskGetCurrentTaskHandle() == lvgl_task_handle);
}

TaskHandle_t lvgl_port_get_task_handle(void) { return lvgl_task_handle; }

// Dummy for compilation compatibility if needed by external calls,
// though AntigravitFix simplifies the flush logic significantly.

// Fallback implementation removed to force linker error if UI is missing
