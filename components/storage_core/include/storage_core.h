#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Magic header: 0xA55A + "SC" (Storage Core)
#define STORAGE_MAGIC 0xA55A5343

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t crc32;
  uint32_t data_len;
  uint32_t reserved[4]; // Future use
} storage_header_t;

/**
 * @brief Calculate CRC32 for data integrity
 */
uint32_t storage_crc32(const uint8_t *data, size_t len);

/**
 * @brief Save data to a file securely (Atomic write + CRC + Versioning)
 *
 * @param path File path (e.g. "/data/config.bin")
 * @param data Data buffer
 * @param len Data length
 * @param version Data version schema (for migration checks)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_save_secure(const char *path, const void *data, size_t len,
                              uint32_t version);

/**
 * @brief Load data from a secure file
 *
 * @param path File path
 * @param out_data Pointer to buffer (will be allocated by function if *out_data
 * is NULL)
 * @param out_len Pointer to write data length
 * @param expected_version Version expected (0 to ignore)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_CRC if corrupt
 */
esp_err_t storage_load_secure(const char *path, void **out_data,
                              size_t *out_len, uint32_t expected_version);

/**
 * @brief Helper for string safe copy
 */
size_t storage_core_strlcpy(char *dst, const char *src, size_t size);

#ifdef __cplusplus
}
#endif
