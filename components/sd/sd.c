/*****************************************************************************
 * | File         :   sd.c
 * | Author       :   Antigravity
 * | Function     :   SD card driver code
 * | Info         :   Fixed for CH422G CS via Shim + Probe
 ******************************************************************************/

#include "sd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "io_extension.h"
#include "sdkconfig.h"
#include <rom/ets_sys.h> // For ets_delay_us
#include <string.h>

static const char *TAG = "sd_shim";

// Persistent Internal State
static spi_host_device_t s_host_id;
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static bool s_spi_bus_inited = false;

// Global accessor for compatibility
sdmmc_card_t *card = NULL;

const char mount_point[] = MOUNT_POINT;

// --- SHIM Context ---
typedef struct {
  int cs_exio_pin;
} sd_shim_ctx_t;

static sd_shim_ctx_t s_shim_ctx;

// --- Low Level CS Control via IO Expander ---
static void sd_shim_cs_set(bool level) {
  // CS Logic: 0 = Selected, 1 = Deselected
  IO_EXTENSION_Output(s_shim_ctx.cs_exio_pin, level ? 1 : 0);
}

// --- DIAGNOSTIC PROBE ---
static esp_err_t sd_spi_probe(spi_host_device_t host_id) {
  ESP_LOGI(TAG, "Starting SD SPI Probe...");

  // 1. Add temp device with NO CS (managed manual)
  spi_device_interface_config_t dev_cfg = {
      .command_bits = 0,
      .address_bits = 0,
      .dummy_bits = 0,
      .clock_speed_hz = 400 * 1000,
      .mode = 0,
      .spics_io_num = -1,
      .queue_size = 1,
  };
  spi_device_handle_t handle;
  if (spi_bus_add_device(host_id, &dev_cfg, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "Probe: Failed to add device");
    return ESP_FAIL;
  }

  // 2. Wake-up clocks (CS High)
  sd_shim_cs_set(true); // CS High
  uint8_t dummy[10];
  memset(dummy, 0xFF, sizeof(dummy));
  spi_transaction_t t_dummy = {.length = 80, .tx_buffer = dummy};
  spi_device_transmit(handle, &t_dummy);

  // 3. Send CMD0 (GO_IDLE_STATE) manually
  // Frame: 0x40 | 0, Arg: 0, CRC: 0x95
  uint8_t cmd0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  // uint8_t resp[6]; // unused

  // Assert CS
  sd_shim_cs_set(false);
  ets_delay_us(100);

  spi_transaction_t t_cmd = {
      .length = 6 * 8, .tx_buffer = cmd0, .rx_buffer = NULL};

  // Explicit TX
  spi_device_transmit(handle, &t_cmd);

  // Poll for R1
  bool resp_ok = false;
  for (int i = 0; i < 16; i++) {
    uint8_t rx = 0xFF;
    spi_transaction_t t_rx = {
        .flags = SPI_TRANS_USE_RXDATA, .length = 8, .rx_data = {0}};
    spi_device_transmit(handle, &t_rx);
    rx = t_rx.rx_data[0];

    // Output every byte for debug
    // ESP_LOGI(TAG, "Probe: Read byte [%d]: 0x%02X", i, rx);
    if (rx != 0xFF) {
      ESP_LOGI(TAG, "Probe: Valid Response Found: 0x%02X", rx);
      if (rx == 0x01) {
        ESP_LOGI(TAG, "Probe: SUCCESS (Idle State)");
      }
      resp_ok = true;
      break;
    }
  }

  // Deassert CS
  sd_shim_cs_set(true);
  // 8 clocks cleanup
  spi_device_transmit(handle, &t_dummy);

  spi_bus_remove_device(handle);

  if (!resp_ok) {
    ESP_LOGE(TAG, "Probe: NO RESPONSE (Check Pins/CS)");
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

// --- SHIMMED TRANSACTION ---
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

static esp_err_t sd_shim_do_transaction_wrapper(int slot,
                                                sdmmc_command_t *cmdinfo) {
  // 1. Assert CS
  sd_shim_cs_set(false);

  // 2. Call original implementation
  esp_err_t ret = s_original_do_transaction(slot, cmdinfo);

  // 3. De-assert CS
  sd_shim_cs_set(true);

  return ret;
}

esp_err_t sd_card_init() {
  if (s_mounted)
    return ESP_OK;

  ESP_LOGI(TAG, "SD Card Init (Shim Mode)...");

  // 1. IO Expander Setup
  s_shim_ctx.cs_exio_pin = CONFIG_ARS_SD_CS_EXIO;

  // Initialize IO Extension just in case
  IO_EXTENSION_Init();

  // Ensure CS High
  sd_shim_cs_set(true);
  vTaskDelay(pdMS_TO_TICKS(10));

  // 2. SPI Bus Init
  s_host_id = SPI2_HOST;
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = CONFIG_ARS_SD_MOSI,
      .miso_io_num = CONFIG_ARS_SD_MISO,
      .sclk_io_num = CONFIG_ARS_SD_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  if (!s_spi_bus_inited) {
    esp_err_t ret = spi_bus_initialize(s_host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret == ESP_OK) {
      s_spi_bus_inited = true;
    } else if (ret == ESP_ERR_INVALID_STATE) {
      s_spi_bus_inited = true;
    } else {
      ESP_LOGE(TAG, "SPI Bus Init Failed: %s", esp_err_to_name(ret));
      return ret;
    }
  }

  // 3. Probe (Diagnostic)
  esp_err_t probe_ret = sd_spi_probe(s_host_id);
  if (probe_ret != ESP_OK) {
    ESP_LOGE(TAG, "Probe Failed - Aborting.");
    return probe_ret;
  }

  // 4. Initialize SDSPI Host & Mount
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = s_host_id;
  host.max_freq_khz = 20000;

  // Inject Shim
  s_original_do_transaction = host.do_transaction;
  host.do_transaction = sd_shim_do_transaction_wrapper;

  // Slot Config
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.host_id = s_host_id;
  slot_config.gpio_cs = -1; // We handle CS manually!

  esp_vfs_fat_mount_config_t mount_config = {.format_if_mount_failed = false,
                                             .max_files = 5,
                                             .allocation_unit_size = 16 * 1024};

  ESP_LOGI(TAG, "Mounting with Shimmed Host...");
  esp_err_t ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config,
                                          &mount_config, &s_card);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Mount Failed: %s", esp_err_to_name(ret));
    return ret;
  }

  s_mounted = true;
  card = s_card;

  sdmmc_card_print_info(stdout, s_card);
  ESP_LOGI(TAG, "Mount Success!");

  return ESP_OK;
}

void sd_card_print_info() {
  if (s_card)
    sdmmc_card_print_info(stdout, s_card);
}

esp_err_t sd_mmc_unmount() {
  if (!s_mounted)
    return ESP_OK;

  esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, s_card);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Unmount Failed: %s", esp_err_to_name(ret));
  }

  s_card = NULL;
  card = NULL;
  s_mounted = false;

  // We do NOT free the SPI bus, as other devices might use it or we might
  // remount
  return ESP_OK;
}

esp_err_t read_sd_capacity(size_t *total_capacity, size_t *available_capacity) {
  FATFS *fs;
  DWORD free_clusters;
  if (f_getfree(mount_point, &free_clusters, &fs) != FR_OK)
    return ESP_FAIL;

  uint64_t total_sectors = ((uint64_t)(fs->n_fatent - 2)) * fs->csize;
  uint64_t free_sectors = ((uint64_t)free_clusters) * fs->csize;

  if (total_capacity)
    *total_capacity = (total_sectors * fs->ssize) / 1024;
  if (available_capacity)
    *available_capacity = (free_sectors * fs->ssize) / 1024;

  return ESP_OK;
}
