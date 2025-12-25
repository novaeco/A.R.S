#include "storage_core.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "storage_core";

uint32_t storage_crc32(const uint8_t *data, size_t len) {
  return esp_crc32_le(0, data, len);
}

size_t storage_core_strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || size == 0)
    return 0;
  if (!src) {
    dst[0] = '\0';
    return 0;
  }
  size_t src_len = strlen(src);
  size_t copy_len = (src_len >= (size - 1)) ? (size - 1) : src_len;
  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';
  return src_len;
}

esp_err_t storage_save_secure(const char *path, const void *data, size_t len,
                              uint32_t version) {
  ESP_RETURN_ON_FALSE(path && data, ESP_ERR_INVALID_ARG, TAG, "Invalid args");

  char tmp_path[128];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

  FILE *f = fopen(tmp_path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open tmp file %s", tmp_path);
    return ESP_FAIL;
  }

  storage_header_t header = {.magic = STORAGE_MAGIC,
                             .version = version,
                             .crc32 = storage_crc32(data, len),
                             .data_len = (uint32_t)len,
                             .reserved = {0}};

  size_t written = fwrite(&header, 1, sizeof(header), f);
  if (written != sizeof(header)) {
    ESP_LOGE(TAG, "Header write failed");
    fclose(f);
    unlink(tmp_path);
    return ESP_FAIL;
  }

  written = fwrite(data, 1, len, f);
  if (written != len) {
    ESP_LOGE(TAG, "Data write failed");
    fclose(f);
    unlink(tmp_path);
    return ESP_FAIL;
  }

  // Fsync to ensure flush to media
  fflush(f);
  fsync(fileno(f));
  fclose(f);

  // Atomic rename
  if (rename(tmp_path, path) != 0) {
    ESP_LOGE(TAG, "Rename failed");
    unlink(tmp_path);
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "Saved %u bytes to %s (v%u)", (unsigned int)len, path,
           (unsigned int)version);
  return ESP_OK;
}

esp_err_t storage_load_secure(const char *path, void **out_data,
                              size_t *out_len, uint32_t expected_version) {
  ESP_RETURN_ON_FALSE(path && out_data && out_len, ESP_ERR_INVALID_ARG, TAG,
                      "Invalid args");

  *out_data = NULL;
  *out_len = 0;

  FILE *f = fopen(path, "rb");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }

  storage_header_t header;
  size_t read = fread(&header, 1, sizeof(header), f);
  if (read != sizeof(header)) {
    ESP_LOGW(TAG, "Header read failed or file too small");
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  if (header.magic != STORAGE_MAGIC) {
    ESP_LOGE(TAG, "Invalid magic 0x%08X", (unsigned int)header.magic);
    fclose(f);
    return ESP_FAIL;
  }

  if (expected_version != 0 && header.version != expected_version) {
    ESP_LOGW(TAG, "Version mismatch: file=%u expected=%u",
             (unsigned int)header.version, (unsigned int)expected_version);
    // We continue, letting caller handle migration if needed, or return error?
    // Usually we might want to return a specific error code
  }

  void *data =
      malloc(header.data_len + 1); // +1 for safety null terminator if string
  if (!data) {
    ESP_LOGE(TAG, "Alloc failed (%u bytes)", (unsigned int)header.data_len);
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  read = fread(data, 1, header.data_len, f);
  fclose(f);

  if (read != header.data_len) {
    ESP_LOGE(TAG, "Data read incomplete");
    free(data);
    return ESP_FAIL;
  }

  uint32_t calc_crc = storage_crc32(data, header.data_len);
  if (calc_crc != header.crc32) {
    ESP_LOGE(TAG, "CRC Mismatch: file=0x%08X calc=0x%08X",
             (unsigned int)header.crc32, (unsigned int)calc_crc);
    free(data);
    return ESP_ERR_INVALID_CRC;
  }

  ((char *)data)[header.data_len] = 0; // Null terminate for safety

  *out_data = data;
  *out_len = header.data_len;

  return ESP_OK;
}
