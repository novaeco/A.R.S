#include "storage_core.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sd_service.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "storage_core";
static const char *NVS_NAMESPACE = "storage";
static const char *NVS_KEY_JSON = "snapshot";
static const char *NVS_KEY_CRC = "snapshot_crc";

static esp_err_t storage_core_persist_locked(storage_context_t *ctx);

static void storage_core_reset(storage_context_t *ctx)
{
    free(ctx->animals);
    free(ctx->documents);
    free(ctx->events);
    memset(ctx, 0, sizeof(*ctx));
}

static size_t storage_core_strlcpy(char *dst, const char *src, size_t size)
{
    if (!dst || !size) {
        return 0;
    }
    size_t src_len = src ? strlen(src) : 0;
    if (src_len + 1 < size) {
        memcpy(dst, src, src_len + 1);
    } else {
        if (size > 0) {
            memcpy(dst, src, size - 1);
            dst[size - 1] = '\0';
        }
    }
    return src_len;
}

static esp_err_t ensure_capacity(void **ptr, size_t elem_size, size_t *capacity, size_t target)
{
    if (target <= *capacity) {
        return ESP_OK;
    }
    size_t new_cap = (*capacity == 0) ? target : (*capacity * 2);
    if (new_cap < target) {
        new_cap = target;
    }
    void *new_buf = realloc(*ptr, elem_size * new_cap);
    if (!new_buf) {
        return ESP_ERR_NO_MEM;
    }
    *ptr = new_buf;
    *capacity = new_cap;
    return ESP_OK;
}

static esp_err_t decode_records_array(cJSON *arr, void **out, size_t elem_size, size_t *count, size_t *capacity,
                                      void (*fill)(void *dst, const cJSON *item))
{
    size_t items = arr ? cJSON_GetArraySize(arr) : 0;
    if (items == 0) {
        free(*out);
        *out = NULL;
        *count = 0;
        *capacity = 0;
        return ESP_OK;
    }
    esp_err_t err = ensure_capacity(out, elem_size, capacity, items);
    if (err != ESP_OK) {
        return err;
    }
    memset(*out, 0, elem_size * (*capacity));
    for (size_t i = 0; i < items; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        fill((uint8_t *)(*out) + (i * elem_size), item);
    }
    *count = items;
    return ESP_OK;
}

static void fill_animal(void *dst, const cJSON *item)
{
    animal_record *a = (animal_record *)dst;
    storage_core_strlcpy(a->id, cJSON_GetStringValue(cJSON_GetObjectItem(item, "id")), sizeof(a->id));
    storage_core_strlcpy(a->species_id, cJSON_GetStringValue(cJSON_GetObjectItem(item, "species_id")), sizeof(a->species_id));
    storage_core_strlcpy(a->sex, cJSON_GetStringValue(cJSON_GetObjectItem(item, "sex")), sizeof(a->sex));
    storage_core_strlcpy(a->birth_date, cJSON_GetStringValue(cJSON_GetObjectItem(item, "birth_date")), sizeof(a->birth_date));
    storage_core_strlcpy(a->acquisition_date, cJSON_GetStringValue(cJSON_GetObjectItem(item, "acquisition_date")), sizeof(a->acquisition_date));
    storage_core_strlcpy(a->origin, cJSON_GetStringValue(cJSON_GetObjectItem(item, "origin")), sizeof(a->origin));
    storage_core_strlcpy(a->status, cJSON_GetStringValue(cJSON_GetObjectItem(item, "status")), sizeof(a->status));
    storage_core_strlcpy(a->identifiers, cJSON_GetStringValue(cJSON_GetObjectItem(item, "identifiers")), sizeof(a->identifiers));
    storage_core_strlcpy(a->location, cJSON_GetStringValue(cJSON_GetObjectItem(item, "location")), sizeof(a->location));
}

static void fill_document(void *dst, const cJSON *item)
{
    document_record *d = (document_record *)dst;
    storage_core_strlcpy(d->id, cJSON_GetStringValue(cJSON_GetObjectItem(item, "id")), sizeof(d->id));
    storage_core_strlcpy(d->type, cJSON_GetStringValue(cJSON_GetObjectItem(item, "type")), sizeof(d->type));
    storage_core_strlcpy(d->scope, cJSON_GetStringValue(cJSON_GetObjectItem(item, "scope")), sizeof(d->scope));
    storage_core_strlcpy(d->reference, cJSON_GetStringValue(cJSON_GetObjectItem(item, "reference")), sizeof(d->reference));
    storage_core_strlcpy(d->file_path, cJSON_GetStringValue(cJSON_GetObjectItem(item, "file_path")), sizeof(d->file_path));
    storage_core_strlcpy(d->fingerprint, cJSON_GetStringValue(cJSON_GetObjectItem(item, "fingerprint")), sizeof(d->fingerprint));
    d->valid = cJSON_IsTrue(cJSON_GetObjectItem(item, "valid"));
}

static void fill_event(void *dst, const cJSON *item)
{
    event_record *e = (event_record *)dst;
    storage_core_strlcpy(e->id, cJSON_GetStringValue(cJSON_GetObjectItem(item, "id")), sizeof(e->id));
    storage_core_strlcpy(e->type, cJSON_GetStringValue(cJSON_GetObjectItem(item, "type")), sizeof(e->type));
    storage_core_strlcpy(e->timestamp, cJSON_GetStringValue(cJSON_GetObjectItem(item, "timestamp")), sizeof(e->timestamp));
    storage_core_strlcpy(e->actor, cJSON_GetStringValue(cJSON_GetObjectItem(item, "actor")), sizeof(e->actor));
    storage_core_strlcpy(e->related_animal, cJSON_GetStringValue(cJSON_GetObjectItem(item, "related_animal")), sizeof(e->related_animal));
    storage_core_strlcpy(e->related_document, cJSON_GetStringValue(cJSON_GetObjectItem(item, "related_document")), sizeof(e->related_document));
    storage_core_strlcpy(e->note, cJSON_GetStringValue(cJSON_GetObjectItem(item, "note")), sizeof(e->note));
}

static esp_err_t decode_payload(storage_context_t *ctx, cJSON *payload)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t version = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(payload, "version"));
    if (version == 0) {
        version = 1;
    }
    cJSON *animals = cJSON_GetObjectItem(payload, "animals");
    cJSON *documents = cJSON_GetObjectItem(payload, "documents");
    cJSON *events = cJSON_GetObjectItem(payload, "events");

    esp_err_t err = decode_records_array(animals, (void **)&ctx->animals, sizeof(animal_record), &ctx->animal_count,
                                         &ctx->animal_capacity, fill_animal);
    if (err != ESP_OK) {
        return err;
    }
    err = decode_records_array(documents, (void **)&ctx->documents, sizeof(document_record), &ctx->document_count,
                               &ctx->document_capacity, fill_document);
    if (err != ESP_OK) {
        return err;
    }
    err = decode_records_array(events, (void **)&ctx->events, sizeof(event_record), &ctx->event_count,
                               &ctx->event_capacity, fill_event);
    if (err != ESP_OK) {
        return err;
    }
    ctx->version = version;
    return ESP_OK;
}

static cJSON *encode_records_array(const void *arr, size_t count, size_t elem_size, cJSON *(*encode)(const void *item))
{
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    const uint8_t *ptr = (const uint8_t *)arr;
    for (size_t i = 0; i < count; ++i) {
        cJSON *obj = encode(ptr + (i * elem_size));
        if (!obj) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddItemToArray(array, obj);
    }
    return array;
}

static cJSON *encode_animal(const void *item)
{
    const animal_record *a = (const animal_record *)item;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    cJSON_AddStringToObject(obj, "id", a->id);
    cJSON_AddStringToObject(obj, "species_id", a->species_id);
    cJSON_AddStringToObject(obj, "sex", a->sex);
    cJSON_AddStringToObject(obj, "birth_date", a->birth_date);
    cJSON_AddStringToObject(obj, "acquisition_date", a->acquisition_date);
    cJSON_AddStringToObject(obj, "origin", a->origin);
    cJSON_AddStringToObject(obj, "status", a->status);
    cJSON_AddStringToObject(obj, "identifiers", a->identifiers);
    cJSON_AddStringToObject(obj, "location", a->location);
    return obj;
}

static cJSON *encode_document(const void *item)
{
    const document_record *d = (const document_record *)item;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    cJSON_AddStringToObject(obj, "id", d->id);
    cJSON_AddStringToObject(obj, "type", d->type);
    cJSON_AddStringToObject(obj, "scope", d->scope);
    cJSON_AddStringToObject(obj, "reference", d->reference);
    cJSON_AddStringToObject(obj, "file_path", d->file_path);
    cJSON_AddStringToObject(obj, "fingerprint", d->fingerprint);
    cJSON_AddBoolToObject(obj, "valid", d->valid);
    return obj;
}

static cJSON *encode_event(const void *item)
{
    const event_record *e = (const event_record *)item;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    cJSON_AddStringToObject(obj, "id", e->id);
    cJSON_AddStringToObject(obj, "type", e->type);
    cJSON_AddStringToObject(obj, "timestamp", e->timestamp);
    cJSON_AddStringToObject(obj, "actor", e->actor);
    cJSON_AddStringToObject(obj, "related_animal", e->related_animal);
    cJSON_AddStringToObject(obj, "related_document", e->related_document);
    cJSON_AddStringToObject(obj, "note", e->note);
    return obj;
}

static esp_err_t encode_payload(const storage_context_t *ctx, cJSON **out_payload, char **out_json, uint32_t *out_crc)
{
    if (!ctx || !out_payload || !out_json || !out_crc) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", ctx->version);

    cJSON *animals = encode_records_array(ctx->animals, ctx->animal_count, sizeof(animal_record), encode_animal);
    cJSON *documents = encode_records_array(ctx->documents, ctx->document_count, sizeof(document_record), encode_document);
    cJSON *events = encode_records_array(ctx->events, ctx->event_count, sizeof(event_record), encode_event);
    if (!animals || !documents || !events) {
        cJSON_Delete(root);
        cJSON_Delete(animals);
        cJSON_Delete(documents);
        cJSON_Delete(events);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "animals", animals);
    cJSON_AddItemToObject(root, "documents", documents);
    cJSON_AddItemToObject(root, "events", events);

    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)json, strlen(json));
    *out_payload = root;
    *out_json = json;
    *out_crc = crc;
    return ESP_OK;
}

static esp_err_t persist_sd(const cJSON *payload, uint32_t crc)
{
    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    if (!mount) {
        return ESP_OK;
    }
    char path[128];
    char tmp_path[140];
    snprintf(path, sizeof(path), "%s/storage.json", mount);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    cJSON *wrapper = cJSON_CreateObject();
    if (!wrapper) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(wrapper, "crc32", crc);
    cJSON *payload_copy = cJSON_Duplicate(payload, true);
    if (!payload_copy) {
        cJSON_Delete(wrapper);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(wrapper, "payload", payload_copy);
    char *json = cJSON_PrintUnformatted(wrapper);
    cJSON_Delete(wrapper);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s", tmp_path);
        free(json);
        return ESP_FAIL;
    }
    size_t written = fwrite(json, 1, strlen(json), f);
    fclose(f);
    free(json);
    if (written == 0) {
        ESP_LOGW(TAG, "Failed to write %s", tmp_path);
        return ESP_FAIL;
    }
    if (rename(tmp_path, path) != 0) {
        ESP_LOGW(TAG, "Rename %s -> %s failed", tmp_path, path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Snapshot persisted to %s", path);
    return ESP_OK;
}

static esp_err_t persist_nvs(const char *json, uint32_t crc)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY_JSON, json, strlen(json) + 1);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, NVS_KEY_CRC, crc);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t storage_core_persist_locked(storage_context_t *ctx)
{
    cJSON *payload = NULL;
    char *json = NULL;
    uint32_t crc = 0;
    esp_err_t err = encode_payload(ctx, &payload, &json, &crc);
    if (err != ESP_OK) {
        return err;
    }
    err = persist_nvs(json, crc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS persist failed: %s", esp_err_to_name(err));
    }
    esp_err_t sd_err = persist_sd(payload, crc);
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "SD persist failed: %s", esp_err_to_name(sd_err));
    }
    cJSON_Delete(payload);
    free(json);
    return (err == ESP_OK) ? sd_err : err;
}

esp_err_t storage_core_init(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    storage_core_reset(ctx);
    ctx->lock = xSemaphoreCreateMutex();
    if (!ctx->lock) {
        return ESP_ERR_NO_MEM;
    }
    ctx->version = 1;
    ESP_LOGI(TAG, "Storage context ready");
    return ESP_OK;
}

static esp_err_t load_from_nvs(storage_context_t *ctx)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t required = 0;
    err = nvs_get_blob(handle, NVS_KEY_JSON, NULL, &required);
    if (err != ESP_OK || required == 0) {
        nvs_close(handle);
        return err;
    }
    char *json = malloc(required);
    if (!json) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    uint32_t stored_crc = 0;
    err = nvs_get_blob(handle, NVS_KEY_JSON, json, &required);
    if (err == ESP_OK) {
        err = nvs_get_u32(handle, NVS_KEY_CRC, &stored_crc);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        free(json);
        return err;
    }
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)json, strlen(json));
    if (crc != stored_crc) {
        ESP_LOGW(TAG, "NVS CRC mismatch (expected %u got %u)", stored_crc, crc);
        free(json);
        return ESP_ERR_INVALID_CRC;
    }
    cJSON *payload = cJSON_Parse(json);
    free(json);
    if (!payload) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = decode_payload(ctx, payload);
    cJSON_Delete(payload);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Storage loaded from NVS");
    }
    return err;
}

static esp_err_t load_from_sd(storage_context_t *ctx)
{
    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    if (!mount) {
        return ESP_ERR_NOT_FOUND;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/storage.json", mount);
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 40960) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t read = fread(json, 1, size, f);
    fclose(f);
    json[read] = '\0';
    cJSON *wrapper = cJSON_Parse(json);
    free(json);
    if (!wrapper) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *payload = cJSON_GetObjectItem(wrapper, "payload");
    cJSON *crc_item = cJSON_GetObjectItem(wrapper, "crc32");
    if (!payload || !crc_item) {
        cJSON_Delete(wrapper);
        return ESP_ERR_INVALID_RESPONSE;
    }
    char *payload_str = cJSON_PrintUnformatted(payload);
    if (!payload_str) {
        cJSON_Delete(wrapper);
        return ESP_ERR_NO_MEM;
    }
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)payload_str, strlen(payload_str));
    uint32_t expected = (uint32_t)cJSON_GetNumberValue(crc_item);
    if (crc != expected) {
        ESP_LOGW(TAG, "SD CRC mismatch (expected %u got %u)", expected, crc);
        free(payload_str);
        cJSON_Delete(wrapper);
        return ESP_ERR_INVALID_CRC;
    }
    cJSON *payload_dup = cJSON_Duplicate(payload, true);
    free(payload_str);
    cJSON_Delete(wrapper);
    if (!payload_dup) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = decode_payload(ctx, payload_dup);
    cJSON_Delete(payload_dup);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Storage loaded from SD");
    }
    return err;
}

esp_err_t storage_core_load(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->lock && xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = load_from_sd(ctx);
    if (err != ESP_OK) {
        err = load_from_nvs(ctx);
    }
    if (err != ESP_OK) {
        ctx->version = 1;
        ESP_LOGW(TAG, "No persisted storage, starting fresh (%s)", esp_err_to_name(err));
    }
    if (ctx->lock) {
        xSemaphoreGive(ctx->lock);
    }
    return err;
}

esp_err_t storage_core_persist(storage_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = storage_core_persist_locked(ctx);
    xSemaphoreGive(ctx->lock);
    return err;
}

static esp_err_t mutate_animal(storage_context_t *ctx, size_t index, const animal_record *record, bool is_update)
{
    if (!ctx || !record) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (is_update) {
        if (index >= ctx->animal_count) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            ctx->animals[index] = *record;
        }
    } else {
        err = ensure_capacity((void **)&ctx->animals, sizeof(animal_record), &ctx->animal_capacity, ctx->animal_count + 1);
        if (err == ESP_OK) {
            ctx->animals[ctx->animal_count] = *record;
            ctx->animal_count++;
        }
    }
    if (err == ESP_OK) {
        ctx->version++;
        err = storage_core_persist_locked(ctx);
    }
    xSemaphoreGive(ctx->lock);
    return err;
}

esp_err_t storage_core_add_animal(storage_context_t *ctx, const animal_record *record)
{
    return mutate_animal(ctx, 0, record, false);
}

esp_err_t storage_core_update_animal(storage_context_t *ctx, size_t index, const animal_record *record)
{
    return mutate_animal(ctx, index, record, true);
}

esp_err_t storage_core_remove_animal(storage_context_t *ctx, size_t index)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (index >= ctx->animal_count) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_SIZE;
    }
    memmove(&ctx->animals[index], &ctx->animals[index + 1], (ctx->animal_count - index - 1) * sizeof(animal_record));
    ctx->animal_count--;
    ctx->version++;
    esp_err_t err = storage_core_persist_locked(ctx);
    xSemaphoreGive(ctx->lock);
    return err;
}

static esp_err_t mutate_document(storage_context_t *ctx, size_t index, const document_record *record, bool is_update)
{
    if (!ctx || !record) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (is_update) {
        if (index >= ctx->document_count) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            ctx->documents[index] = *record;
        }
    } else {
        err = ensure_capacity((void **)&ctx->documents, sizeof(document_record), &ctx->document_capacity, ctx->document_count + 1);
        if (err == ESP_OK) {
            ctx->documents[ctx->document_count] = *record;
            ctx->document_count++;
        }
    }
    if (err == ESP_OK) {
        ctx->version++;
        err = storage_core_persist_locked(ctx);
    }
    xSemaphoreGive(ctx->lock);
    return err;
}

esp_err_t storage_core_add_document(storage_context_t *ctx, const document_record *record)
{
    return mutate_document(ctx, 0, record, false);
}

esp_err_t storage_core_update_document(storage_context_t *ctx, size_t index, const document_record *record)
{
    return mutate_document(ctx, index, record, true);
}

esp_err_t storage_core_remove_document(storage_context_t *ctx, size_t index)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (index >= ctx->document_count) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_SIZE;
    }
    memmove(&ctx->documents[index], &ctx->documents[index + 1], (ctx->document_count - index - 1) * sizeof(document_record));
    ctx->document_count--;
    ctx->version++;
    esp_err_t err = storage_core_persist_locked(ctx);
    xSemaphoreGive(ctx->lock);
    return err;
}

static esp_err_t mutate_event(storage_context_t *ctx, size_t index, const event_record *record, bool is_update)
{
    if (!ctx || !record) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (is_update) {
        if (index >= ctx->event_count) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            ctx->events[index] = *record;
        }
    } else {
        err = ensure_capacity((void **)&ctx->events, sizeof(event_record), &ctx->event_capacity, ctx->event_count + 1);
        if (err == ESP_OK) {
            ctx->events[ctx->event_count] = *record;
            ctx->event_count++;
        }
    }
    if (err == ESP_OK) {
        ctx->version++;
        err = storage_core_persist_locked(ctx);
    }
    xSemaphoreGive(ctx->lock);
    return err;
}

esp_err_t storage_core_add_event(storage_context_t *ctx, const event_record *record)
{
    return mutate_event(ctx, 0, record, false);
}

esp_err_t storage_core_update_event(storage_context_t *ctx, size_t index, const event_record *record)
{
    return mutate_event(ctx, index, record, true);
}

esp_err_t storage_core_remove_event(storage_context_t *ctx, size_t index)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->lock || xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (index >= ctx->event_count) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_SIZE;
    }
    memmove(&ctx->events[index], &ctx->events[index + 1], (ctx->event_count - index - 1) * sizeof(event_record));
    ctx->event_count--;
    ctx->version++;
    esp_err_t err = storage_core_persist_locked(ctx);
    xSemaphoreGive(ctx->lock);
    return err;
}
