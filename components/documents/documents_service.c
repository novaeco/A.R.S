#include "documents_service.h"
#include "esp_log.h"
#include "sd_service.h"

static const char *TAG = "documents";
static const storage_context_t *s_ctx;

void documents_service_init(storage_context_t *ctx)
{
    s_ctx = ctx;
    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    if (mount) {
        ESP_LOGI(TAG, "Documents service will use SD mount %s", mount);
    } else {
        ESP_LOGW(TAG, "Documents service running without SD support");
    }
}

const document_record *documents_service_list(const storage_context_t *ctx, size_t *count)
{
    if (!ctx) {
        return NULL;
    }
    if (count) {
        *count = ctx->document_count;
    }
    return ctx->documents;
}
