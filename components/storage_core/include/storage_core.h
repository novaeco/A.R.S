#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  SemaphoreHandle_t lock;
} storage_context_t;

esp_err_t storage_core_init(storage_context_t *ctx);
esp_err_t storage_core_load(storage_context_t *ctx);
size_t storage_core_strlcpy(char *dst, const char *src, size_t size);

#ifdef __cplusplus
}
#endif
