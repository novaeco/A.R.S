#include "storage_core.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "storage_core";

esp_err_t storage_core_init(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->lock = xSemaphoreCreateMutex();
    if (!ctx->lock) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Storage context ready (in-memory MVP)");
    return ESP_OK;
}

esp_err_t storage_core_persist(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->lock && xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Placeholder for persistence (flash/SD). Keep silent on success.
        xSemaphoreGive(ctx->lock);
    }
    return ESP_OK;
}
