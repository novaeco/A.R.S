#pragma once
#include "storage_core.h"
#include "compliance_rules.h"
#include <esp_err.h>

esp_err_t export_share_init(storage_context_t *ctx);
esp_err_t export_share_animals_csv(const storage_context_t *ctx, const char *path);
