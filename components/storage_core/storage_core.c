#include "storage_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "storage_core";

esp_err_t storage_core_init(storage_context_t *ctx) {
  ESP_RETURN_ON_FALSE(ctx, ESP_ERR_INVALID_ARG, TAG, "ctx is NULL");

  if (!ctx->lock) {
    ctx->lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(ctx->lock, ESP_ERR_NO_MEM, TAG, "lock alloc failed");
  }

  return ESP_OK;
}

esp_err_t storage_core_load(storage_context_t *ctx) {
  ESP_RETURN_ON_FALSE(ctx, ESP_OK == storage_core_init(ctx), TAG,
                      "init failed");
  return ESP_ERR_NOT_FOUND;
}

size_t storage_core_strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || size == 0) {
    return 0;
  }

  if (src == NULL) {
    dst[0] = '\0';
    return 0;
  }

  size_t src_len = strlen(src);
  size_t copy_len = (src_len >= (size - 1)) ? (size - 1) : src_len;
  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';
  return src_len;
}
