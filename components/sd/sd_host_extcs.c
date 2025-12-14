#include "sd_host_extcs.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "io_extension.h"
#include "sd.h"
#include "sdkconfig.h" // Crucial for CONFIG_ARS defines
#include <inttypes.h>
#include <rom/ets_sys.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SD_EXTCS_INIT_FREQ_KHZ 400
#define SD_EXTCS_TARGET_FREQ_KHZ 20000
#define SD_EXTCS_CMD_TIMEOUT_TICKS pdMS_TO_TICKS(150)

static const char *TAG = "sd_extcs";

// --- Internal State ---
static spi_host_device_t s_host_id = SPI2_HOST;
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static spi_device_handle_t s_cleanup_handle = NULL;
static uint32_t s_active_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;

// Pointer to original implementation
static esp_err_t (*s_original_do_transaction)(int slot,
                                              sdmmc_command_t *cmdinfo) = NULL;

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks);
static esp_err_t sd_extcs_low_speed_init(void);

// --- GPIO Helpers ---
// --- GPIO Helpers ---
static esp_err_t sd_extcs_set_cs(bool asserted) {
  // IO4: 0 = Low (Assert), 1 = High (Deassert)
  esp_err_t err = IO_EXTENSION_Output(IO_EXTENSION_IO_4, asserted ? 0 : 1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS %s failed: %s", asserted ? "assert" : "deassert",
             esp_err_to_name(err));
  }
  return err;
}

static void sd_extcs_send_dummy_clocks(size_t byte_count) {
  if (!s_cleanup_handle)
    return;

  uint8_t dummy = 0xFF;
  for (size_t i = 0; i < byte_count; ++i) {
    spi_transaction_t t_cleanup = {
        .length = 8,
        .tx_buffer = &dummy,
        .rx_buffer = NULL,
    };
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }
}

static esp_err_t sd_extcs_wait_for_response(uint8_t *out, TickType_t timeout) {
  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;

  TickType_t start = xTaskGetTickCount();
  uint8_t rx = 0xFF;
  do {
    spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
    esp_err_t err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
    if (err != ESP_OK)
      return err;

    rx = t_rx.rx_data[0];
    if (rx != 0xFF)
      break;
  } while ((xTaskGetTickCount() - start) < timeout);

  *out = rx;
  return (rx == 0xFF) ? ESP_ERR_TIMEOUT : ESP_OK;
}

static esp_err_t sd_extcs_send_command(uint8_t cmd, uint32_t arg, uint8_t crc,
                                       uint8_t *response, size_t response_len,
                                       TickType_t timeout_ticks) {
  if (!s_cleanup_handle)
    return ESP_ERR_INVALID_STATE;

  uint8_t frame[6];
  frame[0] = 0x40 | (cmd & 0x3F);
  frame[1] = (arg >> 24) & 0xFF;
  frame[2] = (arg >> 16) & 0xFF;
  frame[3] = (arg >> 8) & 0xFF;
  frame[4] = arg & 0xFF;
  frame[5] = crc | 0x01; // end bit required

  esp_err_t err = sd_extcs_set_cs(true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CS Assert failed before CMD%u: %s", cmd,
             esp_err_to_name(err));
    return err;
  }

  ets_delay_us(5);

  spi_transaction_t t_cmd = {.length = 48, .tx_buffer = frame};
  err = spi_device_polling_transmit(s_cleanup_handle, &t_cmd);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u TX failed: %s", cmd, esp_err_to_name(err));
    sd_extcs_set_cs(false);
    sd_extcs_send_dummy_clocks(1);
    return err;
  }

  uint8_t first_byte = 0xFF;
  err = sd_extcs_wait_for_response(&first_byte, timeout_ticks);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD%u timeout waiting R1", cmd);
  }

  if (response && response_len > 0) {
    response[0] = first_byte;
    for (size_t i = 1; i < response_len; ++i) {
      spi_transaction_t t_rx = {.flags = SPI_TRANS_USE_RXDATA, .length = 8};
      esp_err_t rx_err = spi_device_polling_transmit(s_cleanup_handle, &t_rx);
      if (rx_err != ESP_OK) {
        err = rx_err;
        break;
      }
      response[i] = t_rx.rx_data[0];
    }
  }

  sd_extcs_set_cs(false);
  sd_extcs_send_dummy_clocks(1);
  ets_delay_us(2);

  return err;
}

// --- Transaction Wrapper ---
static esp_err_t sd_extcs_do_transaction(int slot, sdmmc_command_t *cmdinfo) {
  int64_t t_start_us = 0;

#if CONFIG_ARS_SD_EXTCS_TIMING_LOG
  t_start_us = esp_timer_get_time();
#endif

  // 1. Assert CS
  esp_err_t ret = sd_extcs_set_cs(true);
  if (ret != ESP_OK) {
    // If CS fails (e.g. I2C timeout), we can't trust the transaction.
    // But we must return an error compatible with storage stack.
    ESP_LOGW(TAG, "CS Assert Failed: %s", esp_err_to_name(ret));
    return ESP_ERR_TIMEOUT;
  }

  // Explicit setup time for manual CS
  ets_delay_us(4);

  // 2. Delegate to standard SDSPI implementation
  ret = s_original_do_transaction(slot, cmdinfo);

  // 3. Deassert CS
  sd_extcs_set_cs(false); // Best effort deassert

  // 4. Dummy Clocks (8 cycles)
  if (s_cleanup_handle) {
    // Using polling to be quick and synchronous
    uint8_t dummy = 0xFF;
    spi_transaction_t t_cleanup = {
        .length = 8, .tx_buffer = &dummy, .rx_buffer = NULL};
    spi_device_polling_transmit(s_cleanup_handle, &t_cleanup);
  }
  ets_delay_us(2);

#if CONFIG_ARS_SD_EXTCS_TIMING_LOG
  const uint8_t opcode = cmdinfo ? cmdinfo->opcode : 0xFF;
  const int64_t duration_us = esp_timer_get_time() - t_start_us;
  ESP_LOGI(TAG,
           "CMD%u done in %" PRId64
           " us (len=%d, freq=%u kHz, ret=%s)",
           opcode, duration_us, cmdinfo ? cmdinfo->datalen : 0,
           s_active_freq_khz, esp_err_to_name(ret));
#endif

  return ret;
}

static esp_err_t sd_extcs_low_speed_init(void) {
  gpio_set_pull_mode(CONFIG_ARS_SD_MISO, GPIO_PULLUP_ONLY);

  // Provide 80 clocks with CS high to force SPI mode
  sd_extcs_set_cs(false);
  vTaskDelay(pdMS_TO_TICKS(2));
  sd_extcs_send_dummy_clocks(10);
  ESP_LOGD(TAG, "Sent 80 dummy clocks with CS high before CMD0");

  // CMD0: go idle
  uint8_t resp_r1 = 0xFF;
  esp_err_t err = ESP_FAIL;
  for (int i = 0; i < 8; ++i) {
    err = sd_extcs_send_command(0, 0x00000000, 0x95, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err == ESP_OK && resp_r1 == 0x01)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (resp_r1 != 0x01) {
    ESP_LOGW(TAG,
             "CMD0 failed (resp=0x%02X). Insert SD card or check wiring.",
             resp_r1);
    return ESP_ERR_TIMEOUT;
  }
  ESP_LOGI(TAG, "CMD0: Card entered idle state (R1=0x%02X)", resp_r1);

  // CMD8: check voltage range
  uint8_t resp_r7[5] = {0};
  err = sd_extcs_send_command(8, 0x000001AA, 0x87, resp_r7, sizeof(resp_r7),
                              SD_EXTCS_CMD_TIMEOUT_TICKS);
  bool sdhc_candidate = false;
  if (err == ESP_OK && resp_r7[0] == 0x01) {
    uint32_t pattern = ((uint32_t)resp_r7[1] << 24) |
                       ((uint32_t)resp_r7[2] << 16) |
                       ((uint32_t)resp_r7[3] << 8) | resp_r7[4];
    sdhc_candidate = (pattern == 0x000001AA);
    ESP_LOGI(TAG, "CMD8 OK (pattern=0x%08" PRIX32 ")", pattern);
  } else {
    ESP_LOGW(TAG,
             "CMD8 failed/illegal (resp=0x%02X). Assuming SDSC or older card.",
             resp_r7[0]);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD8 error: %s. Insert SD card or verify 3.3V wiring.",
               esp_err_to_name(err));
    }
  }

  // ACMD41 loop with HCS if supported
  const uint32_t acmd41_arg = sdhc_candidate ? 0x40000000 : 0x00000000;
  bool card_ready = false;
  int acmd41_attempts = 0;
  for (int i = 0; i < 200; ++i) {
    acmd41_attempts = i + 1;
    err = sd_extcs_send_command(55, 0x00000000, 0x65, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CMD55 failed (attempt %d): %s", i + 1,
               esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    err = sd_extcs_send_command(41, acmd41_arg, 0x77, &resp_r1, 1,
                                SD_EXTCS_CMD_TIMEOUT_TICKS);
    if (err == ESP_OK && resp_r1 == 0x00) {
      card_ready = true;
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!card_ready) {
    ESP_LOGE(TAG,
             "ACMD41 timeout. SD card not ready. Insert card or verify cabling.");
    return ESP_ERR_TIMEOUT;
  }
  ESP_LOGI(TAG, "ACMD41 completed in %d attempt(s), card ready", acmd41_attempts);

  // CMD58: read OCR
  uint8_t resp_r3[5] = {0};
  err = sd_extcs_send_command(58, 0x00000000, 0xFD, resp_r3, sizeof(resp_r3),
                              SD_EXTCS_CMD_TIMEOUT_TICKS);
  if (err != ESP_OK || resp_r3[0] != 0x00) {
    ESP_LOGE(TAG,
             "CMD58 failed (resp=0x%02X). Insert SD card or check wiring.",
             resp_r3[0]);
    return ESP_ERR_TIMEOUT;
  }

  uint32_t ocr = ((uint32_t)resp_r3[1] << 24) | ((uint32_t)resp_r3[2] << 16) |
                 ((uint32_t)resp_r3[3] << 8) | resp_r3[4];
  bool high_capacity = (ocr & (1 << 30)) != 0;
  ESP_LOGI(TAG, "OCR=0x%08" PRIX32 " (CCS=%d)", ocr, high_capacity);

  return ESP_OK;
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
  sd_extcs_set_cs(false);
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

  ESP_LOGI(TAG, "SDSPI pins: MISO=%d MOSI=%d SCK=%d CS=IOEXT%d (active-low)",
           CONFIG_ARS_SD_MISO, CONFIG_ARS_SD_MOSI, CONFIG_ARS_SD_SCK,
           IO_EXTENSION_IO_4);

  // 3. Cleanup Device
  if (!s_cleanup_handle) {
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .clock_speed_hz = SD_EXTCS_INIT_FREQ_KHZ * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(s_host_id, &dev_cfg, &s_cleanup_handle);
    if (ret != ESP_OK)
      return ret;
  }

  // 4. Strict low-speed init sequence
  ret = sd_extcs_low_speed_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Pre-init failed. Card may be absent or wiring is wrong.");
    return ret;
  }

  // 5. Host Config
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = s_host_id;
  host.max_freq_khz = SD_EXTCS_INIT_FREQ_KHZ;

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
    s_active_freq_khz = host.max_freq_khz;
    // Verify user can read simple stats
    sdmmc_card_print_info(stdout, s_card);

    // Increase clock after OCR stable and card enumerated
    uint32_t target_khz = SD_EXTCS_TARGET_FREQ_KHZ;
    if (s_card && s_card->max_freq_khz > 0 && s_card->max_freq_khz < target_khz)
      target_khz = s_card->max_freq_khz;

    esp_err_t clk_ret = sdspi_host_set_card_clk(s_host_id, target_khz);
    if (clk_ret == ESP_OK) {
      ESP_LOGI(TAG, "Raised SD SPI clock to %u kHz", target_khz);
      s_active_freq_khz = target_khz;
    } else {
      ESP_LOGW(TAG, "Failed to raise SD SPI clock: %s", esp_err_to_name(clk_ret));
    }
  } else {
    ESP_LOGE(TAG, "Mount Failed: %s. Insert SD card or verify wiring.",
             esp_err_to_name(ret));
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
