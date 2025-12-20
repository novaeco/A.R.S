#include "export_share.h"
#include "domain_models.h"
#include "sd_service.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "export";
static const storage_context_t *s_ctx;

esp_err_t export_share_init(storage_context_t *ctx)
{
    s_ctx = ctx;
    return ESP_OK;
}

esp_err_t export_share_animals_csv(const storage_context_t *ctx, const char *path)
{
    if (!ctx || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s", path);
        return ESP_FAIL;
    }
    fprintf(f, "id,species,sex,birth,origin,status,identifiers,location\n");
    for (size_t i = 0; i < ctx->animal_count; ++i) {
        const animal_record *a = &ctx->animals[i];
        fprintf(f, "%s,%s,%s,%s,%s,%s,%s,%s\n", a->id, a->species_id, a->sex, a->birth_date, a->origin, a->status, a->identifiers, a->location);
    }
    fclose(f);
    ESP_LOGI(TAG, "CSV exported to %s", path);
    return ESP_OK;
}
