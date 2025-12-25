#include "core_service.h"
#include "core_export.h"
#include "data_manager.h"
#include "esp_log.h"
#include "reptile_storage.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "core_service";

esp_err_t core_add_weight(const char *animal_id, float value,
                          const char *unit) {
  // Ignore unit for now, store as float in data_manager (assumed grams)
  ESP_LOGI(TAG, "Adding weight for %s: %.2f", animal_id, value);
  int64_t now = 0; // TODO: Get real time? For now 0 or need time()
  // time(&now);
  // Let's use a dummy timestamp if time is not set, or os timestamp
  struct timeval tv;
  gettimeofday(&tv, NULL);
  now = tv.tv_sec;

  return data_manager_add_weight(animal_id, value, now);
}

esp_err_t core_add_event(const char *animal_id, event_type_t type,
                         const char *description) {
  ESP_LOGI(TAG, "Adding event for %s", animal_id);

  reptile_event_t evt = {0};
  snprintf(evt.reptile_id, sizeof(evt.reptile_id), "%s", animal_id);

  struct timeval tv;
  gettimeofday(&tv, NULL);
  // Simple ID generation
  snprintf(evt.id, sizeof(evt.id), "%lld", (long long)tv.tv_sec);

  evt.type = (int)type;
  evt.timestamp = tv.tv_sec;
  strlcpy(evt.notes, description, sizeof(evt.notes));

  return data_manager_add_event(&evt);
}

esp_err_t core_get_animal(const char *animal_id, animal_t *out_animal) {
  if (!out_animal)
    return ESP_FAIL;

  reptile_t r;
  if (data_manager_load_reptile(animal_id, &r) != ESP_OK) {
    return ESP_ERR_NOT_FOUND;
  }

  // Convert reptile_t to animal_t
  strlcpy(out_animal->id, r.id, sizeof(out_animal->id));
  strlcpy(out_animal->name, r.name, sizeof(out_animal->name));
  strlcpy(out_animal->species, r.species, sizeof(out_animal->species));
  out_animal->sex =
      (r.gender == GENDER_MALE)
          ? SEX_MALE
          : (r.gender == GENDER_FEMALE ? SEX_FEMALE : SEX_UNKNOWN);
  out_animal->dob = (uint32_t)r.birth_date;
  // Origin/Registry not in reptile_t yet, keep empty or defaults
  out_animal->origin[0] = '\0';
  out_animal->registry_id[0] = '\0';

  // Load Weights
  cJSON *w_arr = data_manager_get_weights(animal_id);
  if (w_arr) {
    out_animal->weight_count = cJSON_GetArraySize(w_arr);
    if (out_animal->weight_count > 0) {
      out_animal->weights =
          calloc(out_animal->weight_count, sizeof(weight_entry_t));
      int i = 0;
      cJSON *item = NULL;
      cJSON_ArrayForEach(item, w_arr) {
        cJSON *w_val = cJSON_GetObjectItem(item, "weight");
        cJSON *w_ts = cJSON_GetObjectItem(item, "timestamp");
        if (w_val)
          out_animal->weights[i].value = (float)w_val->valuedouble;
        if (w_ts)
          out_animal->weights[i].date = (uint32_t)w_ts->valuedouble;
        strlcpy(out_animal->weights[i].unit, "g",
                sizeof(out_animal->weights[i].unit));
        i++;
      }
    } else {
      out_animal->weights = NULL;
    }
    cJSON_Delete(w_arr);
  } else {
    out_animal->weight_count = 0;
    out_animal->weights = NULL;
  }

  // Load Events
  cJSON *e_arr = data_manager_get_events(animal_id);
  if (e_arr) {
    out_animal->event_count = cJSON_GetArraySize(e_arr);
    if (out_animal->event_count > 0) {
      out_animal->events =
          calloc(out_animal->event_count, sizeof(event_entry_t));
      int i = 0;
      cJSON *item = NULL;
      cJSON_ArrayForEach(item, e_arr) {
        cJSON *e_type = cJSON_GetObjectItem(item, "type");
        cJSON *e_ts = cJSON_GetObjectItem(item, "timestamp");
        cJSON *e_desc = cJSON_GetObjectItem(item, "notes");

        if (e_type)
          out_animal->events[i].type = (event_type_t)e_type->valueint;
        if (e_ts)
          out_animal->events[i].date = (uint32_t)e_ts->valuedouble;
        if (e_desc)
          strlcpy(out_animal->events[i].description, e_desc->valuestring,
                  sizeof(out_animal->events[i].description));
        i++;
      }
    } else {
      out_animal->events = NULL;
    }
    cJSON_Delete(e_arr);
  } else {
    out_animal->event_count = 0;
    out_animal->events = NULL;
  }

  return ESP_OK;
}

void core_free_animal_content(animal_t *animal) {
  if (animal->weights) {
    free(animal->weights);
    animal->weights = NULL;
  }
  if (animal->events) {
    free(animal->events);
    animal->events = NULL;
  }
  animal->weight_count = 0;
  animal->event_count = 0;
}

esp_err_t core_export_csv(const char *filepath) {
  ESP_LOGI(TAG, "Exporting CSV to %s", filepath);

  FILE *f = fopen(filepath, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open export file: %s", filepath);
    return ESP_FAIL;
  }

  // Header
  fprintf(f, "ID,Name,Species,Sex,DOB,Weight(g)\n");

  cJSON *list = data_manager_list_reptiles();
  if (list) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, list) {
      cJSON *id_obj = cJSON_GetObjectItem(item, "id");
      if (id_obj) {
        reptile_t r;
        if (data_manager_load_reptile(id_obj->valuestring, &r) == ESP_OK) {
          const char *sex_str = (r.gender == GENDER_MALE)
                                    ? "M"
                                    : (r.gender == GENDER_FEMALE ? "F" : "U");
          fprintf(f, "%s,%s,%s,%s,%lld,%.1f\n", r.id, r.name, r.species,
                  sex_str, (long long)r.birth_date, r.weight);
        }
      }
    }
    cJSON_Delete(list);
  }

  fclose(f);
  ESP_LOGI(TAG, "Export complete.");
  return ESP_OK;
}

esp_err_t core_list_animals(animal_summary_t **out_list, size_t *count) {
  cJSON *arr = data_manager_list_reptiles();
  if (!arr)
    return ESP_FAIL;

  *count = cJSON_GetArraySize(arr);
  if (*count == 0) {
    *out_list = NULL;
    cJSON_Delete(arr);
    return ESP_OK;
  }

  *out_list = calloc(*count, sizeof(animal_summary_t));
  if (!*out_list) {
    cJSON_Delete(arr);
    return ESP_ERR_NO_MEM;
  }

  int i = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, arr) {
    cJSON *id = cJSON_GetObjectItem(item, "id");
    cJSON *name = cJSON_GetObjectItem(item, "name");
    if (id) {
      strlcpy((*out_list)[i].id, id->valuestring, sizeof((*out_list)[i].id));
      // Basic load to get species if not in list check
      reptile_t r;
      if (data_manager_load_reptile(id->valuestring, &r) == ESP_OK) {
        strlcpy((*out_list)[i].species, r.species,
                sizeof((*out_list)[i].species));
      } else {
        strlcpy((*out_list)[i].species, "Unknown",
                sizeof((*out_list)[i].species));
      }
    }
    if (name)
      strlcpy((*out_list)[i].name, name->valuestring,
              sizeof((*out_list)[i].name));
    i++;
  }

  cJSON_Delete(arr);
  return ESP_OK;
}

void core_free_animal_list(animal_summary_t *list) { free(list); }

esp_err_t core_list_reports(char ***out_list, size_t *count) {
  ESP_LOGI(TAG, "Stub: core_list_reports");
  if (!out_list || !count)
    return ESP_ERR_INVALID_ARG;

  *count = 2;
  *out_list = calloc(*count, sizeof(char *));
  if (*out_list == NULL)
    return ESP_ERR_NO_MEM;

  (*out_list)[0] = strdup("report_2023_01.csv");
  (*out_list)[1] = strdup("report_2023_02.csv");

  return ESP_OK;
}

void core_free_report_list(char **list, size_t count) {
  if (!list)
    return;
  for (size_t i = 0; i < count; i++) {
    free(list[i]);
  }
  free(list);
}

// Missing implementations

esp_err_t core_search_animals(const char *query, animal_summary_t **out_list,
                              size_t *count) {
  ESP_LOGI(TAG, "Stub: core_search_animals query='%s'", query ? query : "NULL");
  // For now, just return the list of all animals regardless of query
  return core_list_animals(out_list, count);
}

esp_err_t core_save_animal(const animal_t *animal) {
  reptile_t r;
  strlcpy(r.id, animal->id, sizeof(r.id));
  strlcpy(r.name, animal->name, sizeof(r.name));
  strlcpy(r.species, animal->species, sizeof(r.species));
  // r.morph not in animal_t
  r.birth_date = (int64_t)animal->dob;

  if (animal->sex == SEX_MALE)
    r.gender = GENDER_MALE;
  else if (animal->sex == SEX_FEMALE)
    r.gender = GENDER_FEMALE;
  else
    r.gender = GENDER_UNKNOWN;

  r.weight = 0; // Current weight not in animal_t base, computed from history or
                // ignored here

  return data_manager_save_reptile(&r);
}

esp_err_t core_get_alerts(char ***out_list, size_t *count) {
  ESP_LOGI(TAG, "Stub: core_get_alerts");
  if (!out_list || !count)
    return ESP_ERR_INVALID_ARG;

  *count = 1;
  *out_list = calloc(*count, sizeof(char *));
  if (!*out_list)
    return ESP_ERR_NO_MEM;

  (*out_list)[0] = strdup("Température haute (31.5°C)");
  return ESP_OK;
}

void core_free_alert_list(char **list, size_t count) {
  if (!list)
    return;
  for (size_t i = 0; i < count; i++) {
    free(list[i]);
  }
  free(list);
}

esp_err_t core_get_logs(char ***out_list, size_t *count, size_t max) {
  ESP_LOGI(TAG, "Stub: core_get_logs max=%d", (int)max);
  if (!out_list || !count)
    return ESP_ERR_INVALID_ARG;

  *count = 2; // Return dummy logs
  *out_list = calloc(*count, sizeof(char *));
  if (!*out_list)
    return ESP_ERR_NO_MEM;

  // Format: Timestamp|Level|Module|Message
  // Level: 0=Info, 1=Warn, 2=Error
  // Modules: WIFI, SYSTEM, CORE

  // 1. Info
  (*out_list)[0] = strdup("1704067200|0|SYSTEM|Boot complete");
  // 2. Warn
  (*out_list)[1] = strdup("1704067205|1|WIFI|Disconnect reason 201");

  return ESP_OK;
}

void core_free_log_list(char **list, size_t count) {
  if (!list)
    return;
  for (size_t i = 0; i < count; i++) {
    free(list[i]);
  }
  free(list);
}

esp_err_t core_generate_report(const char *animal_id) {
  ESP_LOGI(TAG, "Stub: core_generate_report for %s", animal_id);
  return ESP_OK;
}

esp_err_t core_delete_animal(const char *animal_id) {
  return data_manager_delete_reptile(animal_id);
}
