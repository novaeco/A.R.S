#include "documents_service.h"
#include "esp_log.h"
#include "sd_service.h"
#include "storage_core.h"

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

esp_err_t documents_service_add(storage_context_t *ctx, const document_record *doc)
{
    return storage_core_add_document(ctx, doc);
}

esp_err_t documents_service_update(storage_context_t *ctx, size_t index, const document_record *doc)
{
    return storage_core_update_document(ctx, index, doc);
}

esp_err_t documents_service_remove(storage_context_t *ctx, size_t index)
{
    return storage_core_remove_document(ctx, index);
}
