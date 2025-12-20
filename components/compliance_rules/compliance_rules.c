#include "compliance_rules.h"
#include "sd_service.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "compliance";
static rule_check_result *s_results = NULL;
static size_t s_rule_count = 0;
static uint32_t s_rules_version = 0;

static void free_rules(void)
{
    free(s_results);
    s_results = NULL;
    s_rule_count = 0;
    s_rules_version = 0;
}

static void set_field(char *dst, const char *src, size_t len)
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

static esp_err_t load_rules_from_json(cJSON *root)
{
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *rules = cJSON_GetObjectItem(root, "rules");
    if (!rules || !cJSON_IsArray(rules)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    size_t count = cJSON_GetArraySize(rules);
    if (count == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    rule_check_result *tmp = calloc(count, sizeof(rule_check_result));
    if (!tmp) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(rules, i);
        set_field(tmp[i].id, cJSON_GetStringValue(cJSON_GetObjectItem(item, "id")), sizeof(tmp[i].id));
        set_field(tmp[i].title, cJSON_GetStringValue(cJSON_GetObjectItem(item, "title")), sizeof(tmp[i].title));
        set_field(tmp[i].severity, cJSON_GetStringValue(cJSON_GetObjectItem(item, "severity")), sizeof(tmp[i].severity));
        set_field(tmp[i].scope, cJSON_GetStringValue(cJSON_GetObjectItem(item, "scope")), sizeof(tmp[i].scope));
        set_field(tmp[i].expected_evidence, cJSON_GetStringValue(cJSON_GetObjectItem(item, "expected_evidence")), sizeof(tmp[i].expected_evidence));
    }
    free_rules();
    s_results = tmp;
    s_rule_count = count;
    s_rules_version = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "version"));
    if (s_rules_version == 0) {
        s_rules_version = 1;
    }
    ESP_LOGI(TAG, "Loaded %zu compliance rules (v%u)", s_rule_count, s_rules_version);
    return ESP_OK;
}

static esp_err_t load_rules_from_file(void)
{
    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    if (!mount) {
        return ESP_ERR_NOT_FOUND;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/rules.json", mount);
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16384) {
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
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t err = load_rules_from_json(root);
    cJSON_Delete(root);
    return err;
}

static void load_builtin_rules(void)
{
    free_rules();
    s_rule_count = 3;
    s_results = calloc(s_rule_count, sizeof(rule_check_result));
    if (!s_results) {
        s_rule_count = 0;
        return;
    }
    set_field(s_results[0].id, "R-001", sizeof(s_results[0].id));
    set_field(s_results[0].title, "Document d'origine requis", sizeof(s_results[0].title));
    set_field(s_results[0].severity, "haute", sizeof(s_results[0].severity));
    set_field(s_results[0].scope, "animal", sizeof(s_results[0].scope));
    set_field(s_results[0].expected_evidence, "Certificat", sizeof(s_results[0].expected_evidence));

    set_field(s_results[1].id, "R-002", sizeof(s_results[1].id));
    set_field(s_results[1].title, "Identification marquée", sizeof(s_results[1].title));
    set_field(s_results[1].severity, "moyenne", sizeof(s_results[1].severity));
    set_field(s_results[1].scope, "animal", sizeof(s_results[1].scope));
    set_field(s_results[1].expected_evidence, "RFID", sizeof(s_results[1].expected_evidence));

    set_field(s_results[2].id, "R-003", sizeof(s_results[2].id));
    set_field(s_results[2].title, "Controle sanitaire annuel", sizeof(s_results[2].title));
    set_field(s_results[2].severity, "moyenne", sizeof(s_results[2].severity));
    set_field(s_results[2].scope, "elevage", sizeof(s_results[2].scope));
    set_field(s_results[2].expected_evidence, "Rapport veterinaire", sizeof(s_results[2].expected_evidence));
    s_rules_version = 1;
    ESP_LOGI(TAG, "Builtin rules loaded");
}

void compliance_rules_register_builtin(storage_context_t *ctx)
{
    (void)ctx;
    if (load_rules_from_file() != ESP_OK) {
        load_builtin_rules();
    }
}

static bool evidence_in_documents(const storage_context_t *ctx, const char *needle)
{
    if (!ctx || !needle || !ctx->documents) {
        return false;
    }
    for (size_t i = 0; i < ctx->document_count; ++i) {
        if (strstr(ctx->documents[i].type, needle) || strstr(ctx->documents[i].reference, needle)) {
            return true;
        }
    }
    return false;
}

static bool evidence_in_animals(const storage_context_t *ctx, const char *needle)
{
    if (!ctx || !needle || !ctx->animals) {
        return false;
    }
    for (size_t i = 0; i < ctx->animal_count; ++i) {
        if (strstr(ctx->animals[i].identifiers, needle)) {
            return true;
        }
    }
    return false;
}

const rule_check_result *compliance_rules_evaluate(const storage_context_t *ctx, size_t *count)
{
    if (!s_results || s_rule_count == 0) {
        load_builtin_rules();
    }
    for (size_t i = 0; i < s_rule_count; ++i) {
        bool satisfied = false;
        if (strcmp(s_results[i].scope, "animal") == 0) {
            satisfied = evidence_in_documents(ctx, s_results[i].expected_evidence) ||
                        evidence_in_animals(ctx, s_results[i].expected_evidence);
        } else if (strcmp(s_results[i].scope, "elevage") == 0) {
            satisfied = (ctx && ctx->event_count > 0);
        }
        s_results[i].satisfied = satisfied;
    }
    if (count) {
        *count = s_rule_count;
    }
    return s_results;
}
