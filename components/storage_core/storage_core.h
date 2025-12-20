#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct animal_record;
struct document_record;
struct compliance_report;

typedef struct {
    SemaphoreHandle_t lock;
    struct animal_record *animals;
    size_t animal_count;
    struct document_record *documents;
    size_t document_count;
} storage_context_t;

esp_err_t storage_core_init(storage_context_t *ctx);
esp_err_t storage_core_persist(storage_context_t *ctx);
