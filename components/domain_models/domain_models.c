#include "domain_models.h"
#include "storage_core.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "domain_models";

static void set_string(char *dst, const char *src, size_t len)
{
    if (!dst || !len) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, len);
}

esp_err_t domain_models_bootstrap_if_empty(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->animal_count > 0 || ctx->document_count > 0 || ctx->event_count > 0) {
        return ESP_OK;
    }
    animal_record a = {0};
    set_string(a.id, "A-001", sizeof(a.id));
    set_string(a.species_id, "T-001", sizeof(a.species_id));
    set_string(a.sex, "F", sizeof(a.sex));
    set_string(a.birth_date, "2023-03-12", sizeof(a.birth_date));
    set_string(a.acquisition_date, "2023-05-01", sizeof(a.acquisition_date));
    set_string(a.origin, "Captive bred", sizeof(a.origin));
    set_string(a.status, "Actif", sizeof(a.status));
    set_string(a.identifiers, "RFID:123456789", sizeof(a.identifiers));
    set_string(a.location, "Rack A1", sizeof(a.location));
    esp_err_t err = storage_core_add_animal(ctx, &a);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap animal failed: %s", esp_err_to_name(err));
        return err;
    }

    document_record d = {0};
    set_string(d.id, "D-001", sizeof(d.id));
    set_string(d.type, "Certificat", sizeof(d.type));
    set_string(d.scope, "animal", sizeof(d.scope));
    set_string(d.reference, "REF-2024-01", sizeof(d.reference));
    set_string(d.fingerprint, "stub-sha", sizeof(d.fingerprint));
    d.valid = true;
    err = storage_core_add_document(ctx, &d);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap document failed: %s", esp_err_to_name(err));
        return err;
    }

    event_record e1 = {0};
    set_string(e1.id, "E-001", sizeof(e1.id));
    set_string(e1.type, "Acquisition", sizeof(e1.type));
    set_string(e1.timestamp, "2023-05-01", sizeof(e1.timestamp));
    set_string(e1.actor, "Fournisseur X", sizeof(e1.actor));
    set_string(e1.related_animal, a.id, sizeof(e1.related_animal));
    err = storage_core_add_event(ctx, &e1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap event 1 failed: %s", esp_err_to_name(err));
        return err;
    }

    event_record e2 = {0};
    set_string(e2.id, "E-002", sizeof(e2.id));
    set_string(e2.type, "Mue", sizeof(e2.type));
    set_string(e2.timestamp, "2024-01-10", sizeof(e2.timestamp));
    set_string(e2.actor, "Auto", sizeof(e2.actor));
    set_string(e2.related_animal, a.id, sizeof(e2.related_animal));
    err = storage_core_add_event(ctx, &e2);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap event 2 failed: %s", esp_err_to_name(err));
        return err;
    }

    event_record e3 = {0};
    set_string(e3.id, "E-003", sizeof(e3.id));
    set_string(e3.type, "Controle", sizeof(e3.type));
    set_string(e3.timestamp, "2024-03-05", sizeof(e3.timestamp));
    set_string(e3.actor, "Vet", sizeof(e3.actor));
    set_string(e3.related_animal, a.id, sizeof(e3.related_animal));
    err = storage_core_add_event(ctx, &e3);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap event 3 failed: %s", esp_err_to_name(err));
        return err;
    }

    event_record e4 = {0};
    set_string(e4.id, "E-004", sizeof(e4.id));
    set_string(e4.type, "Document", sizeof(e4.type));
    set_string(e4.timestamp, "2024-05-02", sizeof(e4.timestamp));
    set_string(e4.actor, "Admin", sizeof(e4.actor));
    set_string(e4.related_animal, a.id, sizeof(e4.related_animal));
    set_string(e4.related_document, d.id, sizeof(e4.related_document));
    err = storage_core_add_event(ctx, &e4);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bootstrap event 4 failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bootstrap domain models loaded");
    return ESP_OK;
}

const event_record *domain_models_get_timeline(const storage_context_t *ctx, size_t *count)
{
    if (count) {
        *count = ctx ? ctx->event_count : 0;
    }
    return ctx ? ctx->events : NULL;
}
