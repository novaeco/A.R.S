#include "i2c_bus_shared.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>

static const char *TAG = "i2c_bus_shared";

SemaphoreHandle_t g_i2c_bus_mutex = NULL;
static i2c_master_bus_handle_t s_shared_bus = NULL;
static uint32_t s_consecutive_recover_fails = 0;
static bool s_initialized = false;
static i2c_bus_shared_config_t s_last_cfg = {0};

// Address registry guard (simple array, single bus)
#define I2C_SHARED_MAX_DEVICES 8
typedef struct {
  uint8_t addr;
  i2c_master_dev_handle_t handle;
} i2c_registered_dev_t;
static i2c_registered_dev_t s_registered[I2C_SHARED_MAX_DEVICES] = {0};
static size_t s_registered_count = 0;

static bool addr_is_registered(uint8_t addr7) {
  for (size_t i = 0; i < s_registered_count; ++i) {
    if (s_registered[i].addr == addr7) {
      return true;
    }
  }
  return false;
}

static void register_addr(uint8_t addr7, i2c_master_dev_handle_t handle) {
  if (s_registered_count < I2C_SHARED_MAX_DEVICES) {
    s_registered[s_registered_count].addr = addr7;
    s_registered[s_registered_count].handle = handle;
    s_registered_count++;
  }
}

static void unregister_handle(i2c_master_dev_handle_t handle) {
  for (size_t i = 0; i < s_registered_count; ++i) {
    if (s_registered[i].handle == handle) {
      for (size_t j = i + 1; j < s_registered_count; ++j) {
        s_registered[j - 1] = s_registered[j];
      }
      s_registered[--s_registered_count].addr = 0;
      s_registered[s_registered_count].handle = NULL;
      return;
    }
  }
}

// Forward declaration
static esp_err_t i2c_bus_shared_recover_internal(void);

// Helper: Check if current task holds the recursive mutex
bool i2c_bus_shared_is_locked_by_me(void) {
  if (g_i2c_bus_mutex == NULL)
    return false;
  return xSemaphoreGetMutexHolder(g_i2c_bus_mutex) ==
         xTaskGetCurrentTaskHandle();
}

static void apply_default_config(i2c_bus_shared_config_t *cfg) {
  cfg->port = I2C_BUS_SHARED_DEFAULT_PORT;
  cfg->sda_io_num = I2C_BUS_SHARED_DEFAULT_SDA;
  cfg->scl_io_num = I2C_BUS_SHARED_DEFAULT_SCL;
  cfg->clk_speed_hz = I2C_BUS_SHARED_DEFAULT_CLK_HZ;
  cfg->mutex_timeout_ticks = I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS;
}

esp_err_t i2c_bus_shared_init_with_config(
    const i2c_bus_shared_config_t *cfg_in) {
  i2c_bus_shared_config_t cfg = {0};
  if (cfg_in) {
    cfg = *cfg_in;
  } else {
    apply_default_config(&cfg);
  }
  s_last_cfg = cfg;

  if (s_initialized && s_shared_bus != NULL) {
    return ESP_OK;
  }

  if (g_i2c_bus_mutex == NULL) {
    g_i2c_bus_mutex = xSemaphoreCreateMutex();
    if (!g_i2c_bus_mutex) {
      ESP_LOGE(TAG, "Failed to create I2C mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  // Check for stuck bus at boot
  gpio_set_direction(cfg.sda_io_num, GPIO_MODE_INPUT);
  gpio_set_direction(cfg.scl_io_num, GPIO_MODE_INPUT);
  int sda_level = gpio_get_level(cfg.sda_io_num);
  int scl_level = gpio_get_level(cfg.scl_io_num);

  if (sda_level == 0 || scl_level == 0) {
    ESP_LOGW(TAG, "I2C bus stuck at boot (SDA=%d SCL=%d), recovering...",
             sda_level, scl_level);

    gpio_config_t recover_cfg = {
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = true,
        .pull_down_en = false,
        .pin_bit_mask =
            (1ULL << cfg.scl_io_num) | (1ULL << cfg.sda_io_num),
    };
    gpio_config(&recover_cfg);

    // Toggle SCL
    gpio_set_level(cfg.sda_io_num, 1);
    for (int i = 0; i < 9; i++) {
      gpio_set_level(cfg.scl_io_num, 0);
      esp_rom_delay_us(10);
      gpio_set_level(cfg.scl_io_num, 1);
      esp_rom_delay_us(10);
    }
    // Stop
    gpio_set_level(cfg.sda_io_num, 0);
    esp_rom_delay_us(10);
    gpio_set_level(cfg.scl_io_num, 1);
    esp_rom_delay_us(10);
    gpio_set_level(cfg.sda_io_num, 1);
  }

  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = cfg.port,
      .scl_io_num = cfg.scl_io_num,
      .sda_io_num = cfg.sda_io_num,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
      .intr_priority = 0,
  };

  ESP_LOGI(TAG, "Init shared I2C bus (port=%d SCL=%d SDA=%d freq=%uHz)",
           cfg.port, cfg.scl_io_num, cfg.sda_io_num, cfg.clk_speed_hz);

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
    return false;
  }

  return xSemaphoreTake(g_i2c_bus_mutex, timeout_ticks) == pdTRUE;
}

void i2c_bus_shared_unlock(void) {
  if (g_i2c_bus_mutex) {
    xSemaphoreGive(g_i2c_bus_mutex);
  }
}

i2c_master_bus_handle_t i2c_bus_shared_get_bus(void) { return s_shared_bus; }

SemaphoreHandle_t i2c_bus_shared_get_mutex(void) { return g_i2c_bus_mutex; }

bool i2c_bus_shared_is_ready(void) {
  return s_shared_bus != NULL && s_initialized;
}

static esp_err_t i2c_bus_shared_recover_internal(void) {
  if (s_last_cfg.sda_io_num == 0 && s_last_cfg.scl_io_num == 0) {
    apply_default_config(&s_last_cfg);
  }
  if (s_shared_bus) {
    i2c_del_master_bus(s_shared_bus);
    s_shared_bus = NULL;
  }

  // Manual bit-bang recovery
  gpio_config_t cfg = {
      .mode = GPIO_MODE_OUTPUT_OD,
      .pull_up_en = true,
      .pull_down_en = false,
      .pin_bit_mask =
          (1ULL << s_last_cfg.scl_io_num) | (1ULL << s_last_cfg.sda_io_num),
  };
  gpio_config(&cfg);

  gpio_set_level(s_last_cfg.sda_io_num, 1);
  for (int i = 0; i < 9; i++) {
    gpio_set_level(s_last_cfg.scl_io_num, 0);
    esp_rom_delay_us(10);
    gpio_set_level(s_last_cfg.scl_io_num, 1);
    esp_rom_delay_us(10);
  }
  gpio_set_level(s_last_cfg.sda_io_num, 0);
  esp_rom_delay_us(10);
  gpio_set_level(s_last_cfg.scl_io_num, 1);
  esp_rom_delay_us(10);
  gpio_set_level(s_last_cfg.sda_io_num, 1);

  vTaskDelay(pdMS_TO_TICKS(10));

  s_initialized = false;
  return i2c_bus_shared_init_with_config(&s_last_cfg);
}

esp_err_t i2c_bus_shared_init(void) {
  return i2c_bus_shared_init_with_config(NULL);
}

esp_err_t i2c_bus_shared_recover(void) {
  if (xPortInIsrContext()) {
    return ESP_ERR_INVALID_STATE;
  }

  if (i2c_bus_shared_is_locked_by_me()) {
    return i2c_bus_shared_recover_internal();
  }

  if (!i2c_bus_shared_lock(pdMS_TO_TICKS(1000))) {
    s_consecutive_recover_fails++;
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = i2c_bus_shared_recover_internal();

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

  return i2c_bus_shared_recover_internal();
}

void i2c_bus_shared_deinit(void) {
  if (s_shared_bus) {
    i2c_del_master_bus(s_shared_bus);
    s_shared_bus = NULL;
  }
  if (g_i2c_bus_mutex) {
    vSemaphoreDelete(g_i2c_bus_mutex);
    g_i2c_bus_mutex = NULL;
  }
  s_registered_count = 0;
  s_initialized = false;
}

esp_err_t i2c_bus_shared_add_device(uint8_t addr7, uint32_t scl_hz,
                                    i2c_master_dev_handle_t *out_dev) {
  if (addr7 > 0x7F || out_dev == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_shared_bus) {
    return ESP_ERR_INVALID_STATE;
  }
  if (addr_is_registered(addr7)) {
    ESP_LOGE(TAG, "Device 0x%02X already registered on shared bus", addr7);
    return ESP_ERR_INVALID_STATE;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr7,
      .scl_speed_hz = scl_hz > 0 ? scl_hz : I2C_BUS_SHARED_DEFAULT_CLK_HZ,
  };

  if (!i2c_bus_shared_lock(I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS)) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = i2c_master_bus_add_device(s_shared_bus, &dev_cfg, out_dev);
  if (ret == ESP_OK) {
    register_addr(addr7, *out_dev);
  }
  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_remove_device(i2c_master_dev_handle_t dev_handle) {
  if (!dev_handle) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!i2c_bus_shared_lock(I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS)) {
    return ESP_ERR_TIMEOUT;
  }
  unregister_handle(dev_handle);
  esp_err_t ret = i2c_master_bus_rm_device(dev_handle);
  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_reserve_address(uint8_t addr7) {
  if (addr7 > 0x7F) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!i2c_bus_shared_lock(I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS)) {
    return ESP_ERR_TIMEOUT;
  }
  if (addr_is_registered(addr7)) {
    i2c_bus_shared_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  register_addr(addr7, NULL);
  i2c_bus_shared_unlock();
  return ESP_OK;
}

esp_err_t i2c_bus_shared_release_address(uint8_t addr7) {
  if (addr7 > 0x7F) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!i2c_bus_shared_lock(I2C_BUS_SHARED_DEFAULT_MUTEX_TIMEOUT_TICKS)) {
    return ESP_ERR_TIMEOUT;
  }
  for (size_t i = 0; i < s_registered_count; ++i) {
    if (s_registered[i].addr == addr7) {
      unregister_handle(s_registered[i].handle);
      break;
    }
  }
  i2c_bus_shared_unlock();
  return ESP_OK;
}

esp_err_t i2c_bus_shared_probe(uint8_t addr7, TickType_t timeout_ticks) {
  if (addr7 > 0x7F) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_shared_bus) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!i2c_bus_shared_lock(timeout_ticks)) {
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t ret =
      i2c_master_probe(s_shared_bus, addr7, timeout_ticks);
  i2c_bus_shared_unlock();
  return ret;
}

static esp_err_t txrx_locked(i2c_master_dev_handle_t dev,
                             const uint8_t *tx_buffer, size_t tx_len,
                             uint8_t *rx_buffer, size_t rx_len,
                             TickType_t timeout_ticks) {
  if (!dev) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t ret = ESP_OK;
  if (tx_len > 0 && rx_len > 0) {
    ret = i2c_master_transmit_receive(dev, tx_buffer, tx_len, rx_buffer,
                                      rx_len, timeout_ticks);
  } else if (tx_len > 0) {
    ret = i2c_master_transmit(dev, tx_buffer, tx_len, timeout_ticks);
  } else if (rx_len > 0) {
    ret = i2c_master_receive(dev, rx_buffer, rx_len, timeout_ticks);
  } else {
    ret = ESP_OK;
  }
  return ret;
}

esp_err_t i2c_bus_shared_txrx(i2c_master_dev_handle_t dev,
                              const uint8_t *tx_buffer, size_t tx_len,
                              uint8_t *rx_buffer, size_t rx_len,
                              TickType_t timeout_ticks) {
  if (!i2c_bus_shared_lock(timeout_ticks)) {
    ESP_LOGE(TAG, "Mutex timeout before txrx");
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t ret =
      txrx_locked(dev, tx_buffer, tx_len, rx_buffer, rx_len, timeout_ticks);
  i2c_bus_shared_unlock();
  return ret;
}

esp_err_t i2c_bus_shared_tx(i2c_master_dev_handle_t dev,
                            const uint8_t *tx_buffer, size_t tx_len,
                            TickType_t timeout_ticks) {
  return i2c_bus_shared_txrx(dev, tx_buffer, tx_len, NULL, 0, timeout_ticks);
}

esp_err_t i2c_bus_shared_rx(i2c_master_dev_handle_t dev, uint8_t *rx_buffer,
                            size_t rx_len, TickType_t timeout_ticks) {
  return i2c_bus_shared_txrx(dev, NULL, 0, rx_buffer, rx_len, timeout_ticks);
}
