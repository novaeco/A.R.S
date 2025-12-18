/*****************************************************************************
 * | File         :   i2c.c
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 I2C driver code for I2C communication.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-26
 * | Info         :   Basic version
 *
 ******************************************************************************/

#include "i2c.h"         // Include I2C driver header for I2C functions
#include "esp_rom_sys.h" // For esp_rom_delay_us
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bus_shared.h"

static const char *TAG = "i2c";

// SINGLETON STATE
static i2c_master_bus_handle_t s_bus_handle = NULL;
// g_i2c_bus_mutex is now extern from i2c_bus_shared.h

#define I2C_MUTEX_TIMEOUT_MS 200

// --- Utility Functions ---

esp_err_t DEV_I2C_SanitizeAddr(uint8_t addr, uint8_t *out_addr) {
  if (out_addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (addr > 0x7F) {
    ESP_LOGE(TAG, "I2C address 0x%02X out of 7-bit range", addr);
    return ESP_ERR_INVALID_ARG;
  }

  *out_addr = addr & 0x7F;
  return ESP_OK;
}

bool DEV_I2C_TakeLock(TickType_t wait_ms) {
  return i2c_bus_shared_lock(wait_ms);
}

void DEV_I2C_GiveLock(void) {
  i2c_bus_shared_unlock();
}

// --- Initialization ---

esp_err_t DEV_I2C_Init_Bus(i2c_master_bus_handle_t *out_p_bus_handle) {
  if (s_bus_handle != NULL) {
    if (out_p_bus_handle)
      *out_p_bus_handle = s_bus_handle;
    return ESP_OK;
  }

  i2c_bus_shared_init();

  // Config
  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = ARS_I2C_PORT,
      .scl_io_num = ARS_I2C_SCL,
      .sda_io_num = ARS_I2C_SDA,
      .glitch_ignore_cnt = 7, // Waveshare recommended
      .flags.enable_internal_pullup = true,
  };

  ESP_LOGI(TAG, "Initializing Shared I2C Bus (Port %d, SCL:%d, SDA:%d)",
           ARS_I2C_PORT, ARS_I2C_SCL, ARS_I2C_SDA);

  esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &s_bus_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C New Master Bus Failed: %s", esp_err_to_name(ret));
    return ret;
  }

  if (out_p_bus_handle)
    *out_p_bus_handle = s_bus_handle;
  return ESP_OK;
}

esp_err_t DEV_I2C_Add_Device(uint8_t addr,
                             i2c_master_dev_handle_t *out_dev_handle) {
  if (s_bus_handle == NULL) {
    esp_err_t ret = DEV_I2C_Init_Bus(NULL);
    if (ret != ESP_OK)
      return ret;
  }

  uint8_t san_addr = 0;
  esp_err_t addr_ok = DEV_I2C_SanitizeAddr(addr, &san_addr);
  if (addr_ok != ESP_OK) {
    return addr_ok;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = san_addr,
      .scl_speed_hz = ARS_I2C_FREQUENCY,
  };

  return i2c_master_bus_add_device(s_bus_handle, &dev_cfg, out_dev_handle);
}

// Legacy Init Support
esp_err_t DEV_I2C_Init(DEV_I2C_Port *out_port) {
  if (out_port == NULL)
    return ESP_ERR_INVALID_ARG;

  // Ensure Bus
  esp_err_t ret = DEV_I2C_Init_Bus(&out_port->bus);
  if (ret != ESP_OK)
    return ret;

  // We don't add a default device here anymore unless requested.
  // The legacy code called Add Device commonly.
  // Let's create a dummy device handle if needed or just return OK with NULL
  // device if the caller uses Set_Slave_Addr later (which they usually do).

  // Legacy behavior: The original code added a device at 0x00.
  // Let's replicate that but safer.
  out_port->dev = NULL;
  return ESP_OK;
}

esp_err_t DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *dev_handle,
                                 uint8_t Addr) {
  // In the new driver, we can't just change address of an existing handle
  // easily without potentially removing/adding. However, for this project, the
  // critical change is to sanitize and ADD properly.

  if (*dev_handle != NULL) {
    esp_err_t rm_ret = i2c_master_bus_rm_device(*dev_handle);
    if (rm_ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to remove previous device handle: %s",
               esp_err_to_name(rm_ret));
    }
    *dev_handle = NULL;
  }

  return DEV_I2C_Add_Device(Addr, dev_handle);
}

// --- Data Transmission with Mutex ---

static inline bool i2c_lock() {
  return DEV_I2C_TakeLock(pdMS_TO_TICKS(I2C_MUTEX_TIMEOUT_MS));
}
static inline void i2c_unlock() { DEV_I2C_GiveLock(); }

esp_err_t DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t value) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  uint8_t data[2] = {Cmd, value};

  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret =
      i2c_master_transmit(dev_handle, data, sizeof(data),
                          pdMS_TO_TICKS(1000)); // ARS FIX: 1s timeout
  i2c_unlock();
  return ret;
}

esp_err_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle,
                            uint8_t *value) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_receive(dev_handle, value, 1, pdMS_TO_TICKS(1000));
  i2c_unlock();
  return ret;
}

esp_err_t DEV_I2C_Read_Word(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                            uint16_t *value) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  uint8_t data[2] = {0};
  uint8_t cmd_buf[1] = {Cmd};

  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, cmd_buf, 1, data, 2,
                                              pdMS_TO_TICKS(1000));
  i2c_unlock();

  if (ret == ESP_OK && value) {
    *value = (data[1] << 8) | data[0];
  }
  return ret;
}

esp_err_t DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t dev_handle,
                              uint8_t *pdata, uint8_t len) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret =
      i2c_master_transmit(dev_handle, pdata, len, pdMS_TO_TICKS(1000));
  i2c_unlock();
  return ret;
}

esp_err_t DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t *pdata, uint8_t len) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &Cmd, 1, pdata, len,
                                              pdMS_TO_TICKS(1000));
  i2c_unlock();
  return ret;
}

// --- Recovery ---

esp_err_t DEV_I2C_BusReset(void) {
  // Non-destructive recovery attempted first
  ESP_LOGW(TAG, "Executing Safe I2C Bus Recovery sequence...");

  // We do NOT delete the bus here as it might be used by other threads/tasks.
  // Deleting the bus while a task is waiting on mutex or driver would crash.

  // If we have the lock, great. If not, we can't safely bang bits unless we
  // heavily force it. Given the request is "Non-destructive", and "single bus",
  // we rely on the GLITCH FILTER mostly.

  // However, if the bus is ACTUALLY stuck (SDA Low), we might need bit-banging.
  // We can try to cycle clocks only if we are sure no transaction is live.

  if (i2c_lock()) {
    // Non-destructive reset using IDF driver
    ESP_LOGI(TAG, "Triggering i2c_master_bus_reset...");
    esp_err_t ret = i2c_master_bus_reset(s_bus_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Bus Reset Failed: %s", esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Bus Reset Successful");
    }

    i2c_unlock();
    return ret;
  } else {
    ESP_LOGE(TAG, "Bus Locked by another task, cannot reset.");
    return ESP_ERR_TIMEOUT;
  }
}
