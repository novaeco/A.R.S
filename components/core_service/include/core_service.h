#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "data_manager.h"

// Define types based on usage in ui_animal_details.c

typedef enum { SEX_UNKNOWN, SEX_MALE, SEX_FEMALE } sex_t;

// event_type_t now defined in data_manager.h

typedef struct {
  uint32_t date;
  float value;
  char unit[8];
} weight_entry_t;

typedef struct {
  uint32_t date;
  event_type_t type;
  char description[64];
} event_entry_t;

typedef struct {
  char id[37];
  char name[64];
  char species[64];
  sex_t sex;
  uint32_t dob;
  char origin[64];
  char registry_id[64];

  // Dynamic data handled via pointers or fixed arrays for stub
  // Usage implies getting an animal fills this struct
  // ui_animal_details.c uses direct access to .weights and .events arrays
  // so we must define them here.

  size_t weight_count;
  weight_entry_t *weights; // Pointer to array

  size_t event_count;
  event_entry_t *events; // Pointer to array

} animal_t;

typedef struct {
  char id[37];
  char name[64];
  char species[64];
} animal_summary_t;

// API
esp_err_t core_add_weight(const char *animal_id, float value, const char *unit);
esp_err_t core_add_event(const char *animal_id, event_type_t type,
                         const char *description);
esp_err_t core_get_animal(const char *animal_id, animal_t *out_animal);
esp_err_t core_delete_animal(const char *animal_id);
void core_free_animal_content(animal_t *animal);

// List API
esp_err_t core_list_animals(animal_summary_t **out_list, size_t *count);
void core_free_animal_list(animal_summary_t *list);

esp_err_t core_list_reports(char ***out_list, size_t *count);
void core_free_report_list(char **list, size_t count);

// Missing functions added for compilation fix
esp_err_t core_search_animals(const char *query, animal_summary_t **out_list,
                              size_t *count);
esp_err_t core_save_animal(const animal_t *animal);
esp_err_t core_get_alerts(char ***out_list, size_t *count);
void core_free_alert_list(char **list, size_t count);
esp_err_t core_get_logs(char ***out_list, size_t *count, size_t max);
void core_free_log_list(char **list, size_t count);
esp_err_t core_generate_report(const char *animal_id);
