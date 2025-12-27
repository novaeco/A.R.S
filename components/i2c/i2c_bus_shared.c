#include "i2c_bus_shared.h"
#include "board.h" // Ensure ARS_I2C definitions are visible
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c.h"
#include <assert.h>
#include <inttypes.h>

static const char *TAG = "i2c_bus_shared";

// Recursive mutex to allow recovery within lock
SemaphoreHandle_t g_i2c_bus_mutex = NULL;
static i2c_master_bus_handle_t s_shared_bus = NULL;
static uint32_t s_consecutive_recover_fails = 0;
static bool s_initialized = false;
static portMUX_TYPE s_i2c_stats_spinlock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_i2c_error_streak = 0;
static uint32_t s_i2c_error_total = 0;
static uint32_t s_i2c_success_after_error = 0;
static TickType_t s_i2c_backoff_ticks = 0;
static int64_t s_last_recover_us = 0;

// Forward declaration
static esp_err_t i2c_bus_shared_recover_internal(bool force);

// Helper: Check if current task holds the recursive mutex
bool i2c_bus_shared_is_locked_by_me(void) {
  if (g_i2c_bus_mutex == NULL)
    return false;
  return xSemaphoreGetMutexHolder(g_i2c_bus_mutex) ==
         xTaskGetCurrentTaskHandle();
}

void i2c_bus_shared_note_success(void) {
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  if (s_i2c_error_streak > 0) {
    s_i2c_success_after_error++;
    if (s_i2c_success_after_error >= 4) {
      s_i2c_backoff_ticks = 0;
      s_i2c_error_streak = 0;
      s_i2c_success_after_error = 0;
    }
  }
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);
}

void i2c_bus_shared_note_error(const char *ctx, esp_err_t err) {
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  s_i2c_error_streak++;
  s_i2c_error_total++;
  s_i2c_success_after_error = 0;
  TickType_t next = (s_i2c_backoff_ticks == 0) ? pdMS_TO_TICKS(10)
                                               : (s_i2c_backoff_ticks << 1);
  if (next > pdMS_TO_TICKS(200)) {
    next = pdMS_TO_TICKS(200);
  }
  s_i2c_backoff_ticks = next;
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);

  static int64_t s_last_error_log_us = 0;
  int64_t now = esp_timer_get_time();
  if ((now - s_last_error_log_us) > 500000) { // 500 ms
    s_last_error_log_us = now;
    ESP_LOGW(TAG,
             "I2C shared bus error%s%s%s: %s (streak=%" PRIu32
             " backoff=%" PRIu32 "ms total=%" PRIu32 ")",
             ctx ? " [" : "", ctx ? ctx : "", ctx ? "]" : "",
             esp_err_to_name(err), s_i2c_error_streak,
             (uint32_t)pdTICKS_TO_MS(s_i2c_backoff_ticks), s_i2c_error_total);
  }
}

uint32_t i2c_bus_shared_get_error_streak(void) {
  uint32_t streak = 0;
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  streak = s_i2c_error_streak;
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);
  return streak;
}

TickType_t i2c_bus_shared_backoff_ticks(void) {
  TickType_t ticks = 0;
  portENTER_CRITICAL(&s_i2c_stats_spinlock);
  ticks = s_i2c_backoff_ticks;
  portEXIT_CRITICAL(&s_i2c_stats_spinlock);
  return ticks;
}

esp_err_t i2c_bus_shared_init(void) {
  if (s_initialized && s_shared_bus != NULL) {
    return ESP_OK;
  }

  // Create recursive mutex
  if (g_i2c_bus_mutex == NULL) {
    g_i2c_bus_mutex = xSemaphoreCreateRecursiveMutex();
    if (!g_i2c_bus_mutex) {
      ESP_LOGE(TAG, "Failed to create I2C mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  // Check for stuck bus at boot
  gpio_set_direction(ARS_I2C_SDA, GPIO_MODE_INPUT);
  gpio_set_direction(ARS_I2C_SCL, GPIO_MODE_INPUT);
  int sda_level = gpio_get_level(ARS_I2C_SDA);
  int scl_level = gpio_get_level(ARS_I2C_SCL);

  if (sda_level == 0 || scl_level == 0) {
    ESP_LOGW(TAG, "I2C bus stuck at boot (SDA=%d SCL=%d), recovering...",
             sda_level, scl_level);

    gpio_config_t recover_cfg = {
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = true,
        .pull_down_en = false,
        .pin_bit_mask = (1ULL << ARS_I2C_SCL) | (1ULL << ARS_I2C_SDA),
    };
    gpio_config(&recover_cfg);

    // Toggle SCL
    gpio_set_level(ARS_I2C_SDA, 1);
    for (int i = 0; i < 9; i++) {
      gpio_set_level(ARS_I2C_SCL, 0);
      esp_rom_delay_us(10);
      gpio_set_level(ARS_I2C_SCL, 1);
      esp_rom_delay_us(10);
    }
    // Stop
    gpio_set_level(ARS_I2C_SDA, 0);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SCL, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SDA, 1);
  }

  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = ARS_I2C_PORT,
      .scl_io_num = ARS_I2C_SCL,
      .sda_io_num = ARS_I2C_SDA,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
      .intr_priority = 0,
  };

  ESP_LOGI(TAG, "Init shared I2C bus 0 (SCL=%d SDA=%d)", ARS_I2C_SCL,
           ARS_I2C_SDA);

  esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &s_shared_bus);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init shared I2C bus: %s", esp_err_to_name(ret));
  } else {
    s_initialized = true;
  }

  return ret;
}

bool i2c_bus_shared_lock(TickType_t timeout_ticks) {
  if (xPortInIsrContext()) {
    return false;
  }

  if (g_i2c_bus_mutex == NULL) {
    if (i2c_bus_shared_init() != ESP_OK) {
      return false;
    }
  }

  return xSemaphoreTakeRecursive(g_i2c_bus_mutex, timeout_ticks) == pdTRUE;
}

void i2c_bus_shared_unlock(void) {
  if (g_i2c_bus_mutex) {
    xSemaphoreGiveRecursive(g_i2c_bus_mutex);
  }
}

bool ars_i2c_lock(uint32_t timeout_ms) {
  return i2c_bus_shared_lock(pdMS_TO_TICKS(timeout_ms));
}

void ars_i2c_unlock(void) { i2c_bus_shared_unlock(); }

i2c_master_bus_handle_t i2c_bus_shared_get_handle(void) { return s_shared_bus; }

bool i2c_bus_shared_is_ready(void) {
  return s_shared_bus != NULL && s_initialized;
}

static esp_err_t i2c_bus_shared_recover_internal(bool force) {
  const int64_t now = esp_timer_get_time();
  if (!force) {
    if (s_last_recover_us != 0 &&
        (now - s_last_recover_us) < 200000) { // 200 ms backoff
      ESP_LOGW(TAG, "I2C recover skipped (backoff active)");
      return ESP_ERR_INVALID_STATE;
    }
    s_last_recover_us = now;
  } else {
    s_last_recover_us = now;
    // When forced, drop any accumulated backoff to allow immediate retries.
    portENTER_CRITICAL(&s_i2c_stats_spinlock);
    s_i2c_backoff_ticks = 0;
    portEXIT_CRITICAL(&s_i2c_stats_spinlock);
  }

  if (s_shared_bus == NULL) {
    // Bus never created yet: just initialize without attempting to tear down
    s_initialized = false;
    return i2c_bus_shared_init();
  }

  // Manual bit-bang recovery
  gpio_config_t cfg = {
      .mode = GPIO_MODE_OUTPUT_OD,
      .pull_up_en = true,
      .pull_down_en = false,
      .pin_bit_mask = (1ULL << ARS_I2C_SCL) | (1ULL << ARS_I2C_SDA),
  };
  gpio_config(&cfg);

  gpio_set_level(ARS_I2C_SDA, 1);
  for (int i = 0; i < 9; i++) {
    gpio_set_level(ARS_I2C_SCL, 0);
    esp_rom_delay_us(10);
    gpio_set_level(ARS_I2C_SCL, 1);
    esp_rom_delay_us(10);
  }
  gpio_set_level(ARS_I2C_SDA, 0);
  esp_rom_delay_us(10);
  gpio_set_level(ARS_I2C_SCL, 1);
  esp_rom_delay_us(10);
  gpio_set_level(ARS_I2C_SDA, 1);

  // Keep the pins in open-drain mode with pull-ups to remain compatible with
  // the ESP-IDF I2C master driver configuration without destroying the bus.
  gpio_set_direction(ARS_I2C_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_direction(ARS_I2C_SCL, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(ARS_I2C_SDA, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(ARS_I2C_SCL, GPIO_PULLUP_ONLY);

  vTaskDelay(pdMS_TO_TICKS(5));

  return ESP_OK;
}

esp_err_t i2c_bus_shared_recover(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (i2c_bus_shared_is_locked_by_me()) {
    return i2c_bus_shared_recover_internal(false);
  }

  if (!i2c_bus_shared_lock(pdMS_TO_TICKS(1000))) {
    s_consecutive_recover_fails++;
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = i2c_bus_shared_recover_internal(false);

  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_recover_locked(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  // Caller MUST already hold the mutex
  if (!i2c_bus_shared_is_locked_by_me()) {
    ESP_LOGE(TAG,
             "i2c_bus_shared_recover_locked called without holding mutex!");
    return ESP_ERR_INVALID_STATE;
  }

  return i2c_bus_shared_recover_internal(false);
}

esp_err_t i2c_bus_shared_recover_force(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (i2c_bus_shared_is_locked_by_me()) {
    return i2c_bus_shared_recover_internal(true);
  }

  if (!i2c_bus_shared_lock(pdMS_TO_TICKS(1000))) {
    s_consecutive_recover_fails++;
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = i2c_bus_shared_recover_internal(true);

  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_recover_locked_force(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!i2c_bus_shared_is_locked_by_me()) {
    ESP_LOGE(
        TAG,
        "i2c_bus_shared_recover_locked_force called without holding mutex!");
    return ESP_ERR_INVALID_STATE;
  }

  return i2c_bus_shared_recover_internal(true);
}

void i2c_bus_shared_deinit(void) {
  // Ne jamais supprimer le bus maître tant que des périphériques sont attachés.
  // Le bus reste donc initialisé pour éviter de rompre GT911 / IO extender.
  if (s_shared_bus) {
    ESP_LOGW(TAG, "i2c_bus_shared_deinit skipped (bus kept alive)");
  }
  if (g_i2c_bus_mutex) {
    vSemaphoreDelete(g_i2c_bus_mutex);
    g_i2c_bus_mutex = NULL;
  }
  s_initialized = false;
}
