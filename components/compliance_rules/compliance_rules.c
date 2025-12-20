#include "compliance_rules.h"
#include "domain_models.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "compliance";
static rule_check_result s_results[3];

void compliance_rules_register_builtin(storage_context_t *ctx)
{
    (void)ctx;
    memset(s_results, 0, sizeof(s_results));
    strcpy(s_results[0].id, "R-001");
    strcpy(s_results[0].title, "Document d'origine requis");
    strcpy(s_results[0].severity, "haute");
    strcpy(s_results[0].scope, "animal");
    strcpy(s_results[0].expected_evidence, "Certificat");

    strcpy(s_results[1].id, "R-002");
    strcpy(s_results[1].title, "Identification marquée");
    strcpy(s_results[1].severity, "moyenne");
    strcpy(s_results[1].scope, "animal");
    strcpy(s_results[1].expected_evidence, "RFID");

    strcpy(s_results[2].id, "R-003");
    strcpy(s_results[2].title, "Controle sanitaire annuel");
    strcpy(s_results[2].severity, "moyenne");
    strcpy(s_results[2].scope, "elevage");
    strcpy(s_results[2].expected_evidence, "Rapport veterinaire");
    ESP_LOGI(TAG, "Builtin rules registered");
}

const rule_check_result *compliance_rules_evaluate(const storage_context_t *ctx, size_t *count)
{
    if (count) {
        *count = sizeof(s_results) / sizeof(s_results[0]);
    }
    if (!ctx || !ctx->animals || ctx->animal_count == 0) {
        for (size_t i = 0; i < sizeof(s_results) / sizeof(s_results[0]); ++i) {
            s_results[i].satisfied = false;
        }
        return s_results;
    }

    // Basic heuristic: if a document exists mark rule 001 as satisfied.
    for (size_t i = 0; i < sizeof(s_results) / sizeof(s_results[0]); ++i) {
        s_results[i].satisfied = false;
    }
    if (ctx->documents && ctx->document_count > 0) {
        s_results[0].satisfied = true;
    }
    if (ctx->animals[0].identifiers[0] != '\0') {
        s_results[1].satisfied = true;
    }
    s_results[2].satisfied = false; // pending health report
    return s_results;
}
