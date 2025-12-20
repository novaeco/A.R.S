#pragma once
#include "storage_core.h"
#include "domain_models.h"
#include <stddef.h>

void documents_service_init(storage_context_t *ctx);
const document_record *documents_service_list(const storage_context_t *ctx, size_t *count);
