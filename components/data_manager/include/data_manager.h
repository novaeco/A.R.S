#pragma once

#include <cJSON.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_NAME_LEN 64
#define MAX_SPECIES_LEN 64
#define MAX_ID_LEN 32

_Static_assert(MAX_ID_LEN > 1, "MAX_ID_LEN must allow null termination");
_Static_assert(MAX_NAME_LEN > 1, "MAX_NAME_LEN must allow null termination");
_Static_assert(MAX_SPECIES_LEN > 1,
              "MAX_SPECIES_LEN must allow null termination");

typedef enum { GENDER_MALE, GENDER_FEMALE, GENDER_UNKNOWN } reptile_gender_t;

typedef struct {
  char id[MAX_ID_LEN];
  char name[MAX_NAME_LEN];
  char species[MAX_SPECIES_LEN];
  char morph[MAX_SPECIES_LEN];
  int64_t birth_date; // Timestamp
  reptile_gender_t gender;
  float weight;
  // Add more fields as needed
} reptile_t;

typedef enum {
  EVENT_FEEDING,
  EVENT_MOLT,   // Kept for backward compat if used, mapped to SHEDDING conceptually or distinct
  EVENT_VET,
  EVENT_BREEDING, // distinct
  EVENT_OTHER,
  EVENT_SHEDDING,
  EVENT_CLEANING,
  EVENT_MATING,
  EVENT_LAYING,
  EVENT_HATCHING
} event_type_t;

typedef struct {
  char id[MAX_ID_LEN]; // Event ID
  char reptile_id[MAX_ID_LEN];
  event_type_t type;
  int64_t timestamp;
  char notes[256];
} reptile_event_t;

// API
esp_err_t data_manager_init(void);
bool data_manager_is_ready(void);

// Reptile Operations
esp_err_t data_manager_save_reptile(const reptile_t *reptile);
esp_err_t data_manager_load_reptile(const char *id, reptile_t *out_reptile);
esp_err_t data_manager_delete_reptile(const char *id);
// Returns a list of IDs. Caller must free cJSON object.
cJSON *data_manager_list_reptiles(void);

// Event Operations
esp_err_t data_manager_add_event(const reptile_event_t *event);
cJSON *data_manager_get_events(const char *reptile_id);

// Weight Operations
esp_err_t data_manager_add_weight(const char *reptile_id, float weight,
                                  int64_t timestamp);
cJSON *data_manager_get_weights(const char *reptile_id);

// Utils
const char *gender_to_str(reptile_gender_t gender);
