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
#define I2C_PROBE_TIMEOUT_MS 100
#define I2C_XFER_TIMEOUT_MS 200

// Simple device address registry to warn about duplicate registrations
#define I2C_MAX_DEVICES 8
static uint8_t s_registered_addrs[I2C_MAX_DEVICES] = {0};
static size_t s_registered_count = 0;

static bool is_addr_registered(uint8_t addr) {
  for (size_t i = 0; i < s_registered_count; i++) {
    if (s_registered_addrs[i] == addr)
      return true;
  }
  return false;
}

static void register_addr(uint8_t addr) {
  if (s_registered_count < I2C_MAX_DEVICES) {
    s_registered_addrs[s_registered_count++] = addr;
  }
}

static inline bool i2c_lock() {
  return DEV_I2C_TakeLock(pdMS_TO_TICKS(I2C_MUTEX_TIMEOUT_MS));
}
static inline void i2c_unlock() { DEV_I2C_GiveLock(); }

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

void DEV_I2C_GiveLock(void) { i2c_bus_shared_unlock(); }

esp_err_t DEV_I2C_Probe(i2c_master_bus_handle_t bus_handle, uint8_t addr) {
  if (bus_handle == NULL) {
    bus_handle = s_bus_handle;
  }

  if (bus_handle == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t san_addr = 0;
  esp_err_t ret = DEV_I2C_SanitizeAddr(addr, &san_addr);
  if (ret != ESP_OK) {
    return ret;
  }

  if (!DEV_I2C_TakeLock(pdMS_TO_TICKS(I2C_MUTEX_TIMEOUT_MS))) {
    return ESP_ERR_TIMEOUT;
  }

  ret = i2c_master_probe(bus_handle, san_addr,
                         pdMS_TO_TICKS(I2C_PROBE_TIMEOUT_MS));
  DEV_I2C_GiveLock();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C probe 0x%02X failed: %s", san_addr,
             esp_err_to_name(ret));
  } else {
    ESP_LOGD(TAG, "I2C probe 0x%02X OK", san_addr);
  }

  return ret;
}

// --- Initialization ---

esp_err_t DEV_I2C_Init_Bus(i2c_master_bus_handle_t *out_p_bus_handle) {
  esp_err_t ret = i2c_bus_shared_init();
  if (ret != ESP_OK) {
    return ret;
  }

  if (s_bus_handle != NULL) {
    if (out_p_bus_handle) {
      *out_p_bus_handle = s_bus_handle;
    }
    return ESP_OK;
  }

  s_bus_handle = i2c_bus_shared_get_handle();
  if (s_bus_handle == NULL) {
    ESP_LOGE(TAG, "Shared I2C bus handle unavailable after init");
    return ESP_ERR_INVALID_STATE;
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

  // Warn if attempting to re-register same address (may indicate driver bug)
  if (is_addr_registered(san_addr)) {
    ESP_LOGW(TAG, "Device 0x%02X already registered on bus", san_addr);
  } else {
    register_addr(san_addr);
    ESP_LOGD(TAG, "Registered device 0x%02X on shared bus", san_addr);
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

  esp_err_t ret = DEV_I2C_Init_Bus(NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  if (!i2c_lock()) {
    return ESP_ERR_TIMEOUT;
  }

  if (*dev_handle != NULL) {
    esp_err_t rm_ret = i2c_master_bus_rm_device(*dev_handle);
    if (rm_ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to remove previous device handle: %s",
               esp_err_to_name(rm_ret));
    }
    *dev_handle = NULL;
  }

  ret = DEV_I2C_Add_Device(Addr, dev_handle);
  i2c_unlock();
  return ret;
}

// --- Data Transmission with Mutex ---

esp_err_t DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t value) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  uint8_t data[2] = {Cmd, value};

  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_transmit(dev_handle, data, sizeof(data),
                                      pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
  i2c_unlock();
  return ret;
}

esp_err_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle,
                            uint8_t *value) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_receive(dev_handle, value, 1,
                                     pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
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
  esp_err_t ret = i2c_master_transmit_receive(
      dev_handle, cmd_buf, 1, data, 2, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
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
  esp_err_t ret = i2c_master_transmit(dev_handle, pdata, len,
                                      pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
  i2c_unlock();
  return ret;
}

esp_err_t DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd,
                             uint8_t *pdata, uint8_t len) {
  if (!dev_handle)
    return ESP_ERR_INVALID_ARG;
  if (!i2c_lock())
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = i2c_master_transmit_receive(
      dev_handle, &Cmd, 1, pdata, len, pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
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

  return i2c_bus_shared_recover();
}
