#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "domain_models.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct storage_context {
    SemaphoreHandle_t lock;
    uint32_t version;
    animal_record *animals;
    size_t animal_count;
    size_t animal_capacity;
    document_record *documents;
    size_t document_count;
    size_t document_capacity;
    event_record *events;
    size_t event_count;
    size_t event_capacity;
} storage_context_t;

esp_err_t storage_core_init(storage_context_t *ctx);
esp_err_t storage_core_load(storage_context_t *ctx);
esp_err_t storage_core_persist(storage_context_t *ctx);

esp_err_t storage_core_add_animal(storage_context_t *ctx, const animal_record *record);
esp_err_t storage_core_update_animal(storage_context_t *ctx, size_t index, const animal_record *record);
esp_err_t storage_core_remove_animal(storage_context_t *ctx, size_t index);

esp_err_t storage_core_add_document(storage_context_t *ctx, const document_record *record);
esp_err_t storage_core_update_document(storage_context_t *ctx, size_t index, const document_record *record);
esp_err_t storage_core_remove_document(storage_context_t *ctx, size_t index);

esp_err_t storage_core_add_event(storage_context_t *ctx, const event_record *record);
esp_err_t storage_core_update_event(storage_context_t *ctx, size_t index, const event_record *record);
esp_err_t storage_core_remove_event(storage_context_t *ctx, size_t index);
