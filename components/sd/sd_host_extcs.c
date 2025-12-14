#include "sd_host_extcs.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "io_extension.h"
#include "sd.h"
#include "sdkconfig.h" // Crucial for CONFIG_ARS defines
#include <rom/ets_sys.h>
#include <string.h>

static const char *TAG = "sd_extcs";

// --- Internal State ---
static spi_host_device_t s_host_id = SPI2_HOST;
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static spi_device_handle_t s_cleanup_handle = NULL;

// Pointer to original implementation
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

// --- GPIO Helpers ---
// --- GPIO Helpers ---
static esp_err_t cs_set_level(bool level) {
  // IO4: 0 = Low (Assert), 1 = High (Deassert)
  return IO_EXTENSION_Output(IO_EXTENSION_IO_4, level ? 1 : 0);
}

// --- Transaction Wrapper ---
static esp_err_t sd_extcs_do_transaction(int slot, sdmmc_command_t *cmdinfo) {
  // 1. Assert CS
  esp_err_t ret = cs_set_level(false);
  if (ret != ESP_OK) {
    // If CS fails (e.g. I2C timeout), we can't trust the transaction.
    // But we must return an error compatible with storage stack.
    ESP_LOGW(TAG, "CS Assert Failed: %s", esp_err_to_name(ret));
    return ESP_ERR_TIMEOUT;
  }

  // Explicit setup time for manual CS
  ets_delay_us(50);

  // 2. Delegate to standard SDSPI implementation
  ret = s_original_do_transaction(slot, cmdinfo);

  // 3. Deassert CS
  cs_set_level(true); // Best effort deassert

  // 4. Dummy Clocks (8 cycles)
  if (s_cleanup_handle) {
    // Using polling to be quick and synchronous
    uint8_t dummy = 0xFF;
    spi_transaction_t t_cleanup = {
        .length = 8, .tx_buffer = &dummy, .rx_buffer = NULL};
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }

  return ret;
}

// --- Probe Helper (Diagnostic) ---
static esp_err_t sd_extcs_probe(void) {
  ESP_LOGI(TAG, "Probing SD Card (Strict Mode)...");

  // Configure MISO pullup explicitly
  gpio_set_pull_mode(CONFIG_ARS_SD_MISO, GPIO_PULLUP_ONLY);

  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;

  // 1. Strict Sequence: CS HIGH + 74 (80) Clocks
  cs_set_level(true);
  vTaskDelay(pdMS_TO_TICKS(2)); // Allow stabilization

  uint8_t dummy[10];
  memset(dummy, 0xFF, sizeof(dummy));
  spi_transaction_t t_wake = {.length = 80, .tx_buffer = dummy};
  spi_device_polling_transmit(s_cleanup_handle, &t_wake);

  // 2. CMD0 Loop
  uint8_t cmd0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  bool success = false;
  uint8_t last_rx = 0xFF;

  // Retry loop
  for (int i = 0; i < 5; i++) {
    esp_err_t err = cs_set_level(false);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Probe: CS Assert Error");
      return err;
    }
    ets_delay_us(500); // Allow CS to settle

    spi_transaction_t t_cmd = {.length = 48, .tx_buffer = cmd0};
    spi_device_polling_transmit(s_cleanup_handle, &t_cmd);

    // Read response (R1 is 1 byte, but we read up to 8 to catch it)
    for (int j = 0; j < 8; j++) {
      uint8_t rx = 0xFF;
      spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
      spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      rx = t_rx.rx_data[0];
      last_rx = rx;

      if (rx != 0xFF) {
        // R1 Idle = 0x01
        if (rx == 0x01) {
          success = true;
          ESP_LOGI(TAG, "Probe: CMD0 Response 0x01 (OK)");
        } else {
          ESP_LOGW(TAG, "Probe: Invalid Response 0x%02X", rx);
        }
        break;
      }
    }

    cs_set_level(true);
    // 8 dummy clocks after CS High
    spi_transaction_t t_dummy = {.length = 8, .tx_buffer = dummy};
    spi_device_polling_transmit(s_cleanup_handle, &t_dummy);

    if (success)
      break;

    // If we only saw 0xFF, MISO might be stuck high.
    // If we saw 0x00 or garbage, logging happens above.
    vTaskDelay(pdMS_TO_TICKS(20)); // Slower retry
  }

  if (success) {
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Probe: Failed. Last Byte=0x%02X", last_rx);
    // Diagnostic hint
    if (last_rx == 0xFF)
      ESP_LOGW(TAG, "Hint: MISO stuck HIGH? Check Pullups/Connection.");
    if (last_rx == 0x00)
      ESP_LOGW(TAG, "Hint: MISO stuck LOW? Check Wiring.");
    return ESP_ERR_TIMEOUT;
  }
}

// --- Public API ---

esp_err_t sd_extcs_mount_card(const char *mount_point, size_t max_files) {
  if (s_mounted)
    return ESP_OK;

  esp_err_t ret;

  // 1. IO Init (Safe-Fail)
  ret = IO_EXTENSION_Init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG,
             "SD: ExtCS Mode unavailable (IOEXT Init Failed). Aborting SD.");
    return ESP_FAIL;
  }
  cs_set_level(true);
  vTaskDelay(pdMS_TO_TICKS(10));

  // 2. SPI Bus Init
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = CONFIG_ARS_SD_MOSI,
      .miso_io_num = CONFIG_ARS_SD_MISO,
      .sclk_io_num = CONFIG_ARS_SD_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  ret = spi_bus_initialize(s_host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "SPI Bus Init Failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // 3. Cleanup Device
  if (!s_cleanup_handle) {
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .clock_speed_hz = 400 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(s_host_id, &dev_cfg, &s_cleanup_handle);
    if (ret != ESP_OK)
      return ret;
  }

  // 4. Probe
  sd_extcs_probe(); // Ignore return, try mount anyway but logs will show status

  // 5. Host Config
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = s_host_id;
  host.max_freq_khz = 20000;

  s_original_do_transaction = host.do_transaction;
  host.do_transaction = sd_extcs_do_transaction;

  // 6. Slot Config (Disable CS)
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.host_id = s_host_id;
  slot_config.gpio_cs = -1;

  // 7. Mount
  esp_vfs_fat_mount_config_t mount_config = {.format_if_mount_failed = false,
                                             .max_files = max_files,
                                             .allocation_unit_size = 16 * 1024};

  ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config,
                                &s_card);

  if (ret == ESP_OK) {
    s_mounted = true;
    // Verify user can read simple stats
    sdmmc_card_print_info(stdout, s_card);
  } else {
    ESP_LOGE(TAG, "Mount Failed: %s", esp_err_to_name(ret));
    s_card = NULL;
  }

  return ret;
}

esp_err_t sd_extcs_unmount_card(const char *mount_point) {
  if (!s_mounted)
    return ESP_OK;

  esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, s_card);
  if (ret == ESP_OK) {
    s_card = NULL;
    s_mounted = false;

    // Remove cleanup device
    if (s_cleanup_handle) {
      spi_bus_remove_device(s_cleanup_handle);
      s_cleanup_handle = NULL;
    }

    // Note: We do NOT free the SPI bus because other devices (LCD?) might
    // technically use it But here SPI2_HOST is exclusively for SD in most
    // setups. If we want to free: spi_bus_free(s_host_id);
  }
  return ret;
}

sdmmc_card_t *sd_extcs_get_card_handle(void) { return s_card; }
