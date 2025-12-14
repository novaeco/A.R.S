#pragma once

#include "driver/sdmmc_types.h"
#include "driver/sdspi_host.h"
#include "driver/i2c_master.h"
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

// API for external CS SD Host
// Handles mounting using SDSPI with manual CS control via IO_EXTENSION (IO4)

typedef enum {
  SD_EXTCS_STATE_UNINITIALIZED = 0,
  SD_EXTCS_STATE_IDLE_READY,
  SD_EXTCS_STATE_ABSENT,
  SD_EXTCS_STATE_INIT_FAIL,
} sd_extcs_state_t;

/**
 * @brief Initialize and mount the SD card using the external CS wrapper.
 *
 * @param mount_point VFS mount point (e.g., "/sdcard")
 * @param max_files Maximum open files
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sd_extcs_mount_card(const char *mount_point, size_t max_files);

/**
 * @brief Unmount the card and free resources
 * @param mount_point VFS mount point used during init
 */
esp_err_t sd_extcs_unmount_card(const char *mount_point);

/**
 * @brief Get the card handle
 */
sdmmc_card_t *sd_extcs_get_card_handle(void);

/**
 * @brief Register an already initialized IO expander handle for CS control.
 */
esp_err_t sd_extcs_register_io_extender(i2c_master_dev_handle_t handle);

/**
 * @brief Get last known ExtCS state (absence vs init failure vs ready).
 */
sd_extcs_state_t sd_extcs_get_state(void);
const char *sd_extcs_state_str(sd_extcs_state_t state);

#ifdef __cplusplus
}
#endif
