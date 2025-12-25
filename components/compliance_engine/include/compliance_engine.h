#pragma once

#include "data_manager.h"
#include "esp_err.h"


// Compliance Status
typedef enum {
  COMPLIANCE_OK,
  COMPLIANCE_WARNING,
  COMPLIANCE_NON_COMPLIANT,
  COMPLIANCE_UNKNOWN
} compliance_status_t;

typedef struct {
  compliance_status_t status;
  char message[128];
  char missing_doc_type[32]; // e.g. "CITES_IMPORT"
} compliance_report_t;

/**
 * @brief Initialize the compliance engine (load rules)
 */
esp_err_t compliance_engine_init(void);

/**
 * @brief Check compliance for a specific animal
 *
 * @param animal_id ID of the reptile
 * @param out_report Pointer to report structure
 * @return esp_err_t ESP_OK if check completed (check status in report)
 */
esp_err_t compliance_check_animal(const char *animal_id,
                                  compliance_report_t *out_report);

/**
 * @brief Check compliance for the entire facility (all animals)
 *
 * @return compliance_status_t Global status
 */
compliance_status_t compliance_check_facility(void);
