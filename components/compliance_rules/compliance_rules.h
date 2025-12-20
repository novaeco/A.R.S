#pragma once
#include "storage_core.h"
#include <stddef.h>

typedef struct {
    char id[16];
    char title[64];
    char severity[16];
    char scope[32];
    char expected_evidence[64];
    bool satisfied;
} rule_check_result;

void compliance_rules_register_builtin(storage_context_t *ctx);
const rule_check_result *compliance_rules_evaluate(const storage_context_t *ctx, size_t *count);
