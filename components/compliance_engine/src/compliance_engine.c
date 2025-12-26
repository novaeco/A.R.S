#include "compliance_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "compliance";

esp_err_t compliance_engine_init(void) {
  ESP_LOGI(TAG, "Initializing Compliance Engine...");
  // Load rules from storage or hardcoded DB
  return ESP_OK;
}

esp_err_t compliance_check_animal(const char *animal_id,
                                  compliance_report_t *out_report) {
  if (!out_report)
    return ESP_ERR_INVALID_ARG;

  memset(out_report, 0, sizeof(compliance_report_t));
  out_report->status = COMPLIANCE_OK; // Optimistic default

  reptile_t r;
  if (data_manager_load_reptile(animal_id, &r) != ESP_OK) {
    snprintf(out_report->message, sizeof(out_report->message),
             "Animal not found");
    out_report->status = COMPLIANCE_UNKNOWN;
    return ESP_ERR_NOT_FOUND;
  }

  // Example Rule: All "Python regius" must have origin documents
  // This is a stub logic
  if (strcasecmp(r.species, "Python regius") == 0 ||
      strstr(r.species, "Python")) {
    // Query documents for this animal
    cJSON *docs = data_manager_list_documents(r.id);
    if (!docs || cJSON_GetArraySize(docs) == 0) {
      out_report->status = COMPLIANCE_WARNING;
      snprintf(out_report->message, sizeof(out_report->message),
               "Missing origin proof for %s", r.species);
      strlcpy(out_report->missing_doc_type, "ORIGIN_PROOF",
              sizeof(out_report->missing_doc_type));
    }
    if (docs)
      cJSON_Delete(docs);
  }

  return ESP_OK;
}

compliance_status_t compliance_check_facility(void) {
  // Stub: Iterate all animals and Aggregate status
  return COMPLIANCE_OK;
}
