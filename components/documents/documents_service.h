#pragma once
#include "storage_core.h"
#include "domain_models.h"
#include <stddef.h>

void documents_service_init(storage_context_t *ctx);
const document_record *documents_service_list(const storage_context_t *ctx, size_t *count);
esp_err_t documents_service_add(storage_context_t *ctx, const document_record *doc);
esp_err_t documents_service_update(storage_context_t *ctx, size_t index, const document_record *doc);
esp_err_t documents_service_remove(storage_context_t *ctx, size_t index);
