#include "data_manager.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "data_manager";
static const char *MOUNT_POINT = "/data";

#ifndef CONFIG_ARS_DATA_MAX_JSON_SIZE
#define CONFIG_ARS_DATA_MAX_JSON_SIZE 8192
#endif

static SemaphoreHandle_t s_data_fs_lock = NULL;
static bool s_storage_ready = false;
static bool s_storage_warned = false;

static bool data_fs_lock(TickType_t timeout_ticks) {
  if (!s_data_fs_lock) {
    return false;
  }
  return xSemaphoreTake(s_data_fs_lock, timeout_ticks) == pdTRUE;
}

static void data_fs_unlock(void) {
  if (s_data_fs_lock) {
    xSemaphoreGive(s_data_fs_lock);
  }
}

static esp_err_t ensure_directory(const char *path) {
  struct stat st = {0};
  if (stat(path, &st) == -1) {
    if (mkdir(path, 0775) != 0) {
      ESP_LOGE(TAG, "Failed to create directory %s (errno=%d)", path, errno);
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static inline void copy_bounded(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strlcpy(dst, src, dst_size);
}

static inline bool storage_ready_guard(const char *context) {
  if (s_storage_ready) {
    return true;
  }
  if (!s_storage_warned) {
    ESP_LOGW(TAG, "%s: storage unavailable", context);
    s_storage_warned = true;
  }
  return false;
}

bool data_manager_is_ready(void) { return s_storage_ready; }

esp_err_t data_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Data Manager");

  s_storage_ready = false;
  esp_vfs_littlefs_conf_t conf = {
      .base_path = MOUNT_POINT,
      .partition_label = "storage",
      .format_if_mount_failed = true,
      .dont_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find LittleFS partition with label '%s'", conf.partition_label);
    } else {
      ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    }
    ESP_LOGW(TAG,
             "LittleFS unavailable; continuing without persistent storage");
    return ret;
  }

  size_t total = 0, used = 0;
  ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }

  s_data_fs_lock = xSemaphoreCreateMutex();
  if (!s_data_fs_lock) {
    ESP_LOGE(TAG, "Failed to create filesystem mutex");
    return ESP_ERR_NO_MEM;
  }

  // Ensure directories exist
  ESP_RETURN_ON_ERROR(ensure_directory("/data/reptiles"), TAG,
                      "failed to create reptiles dir");
  ESP_RETURN_ON_ERROR(ensure_directory("/data/events"), TAG,
                      "failed to create events dir");
  ESP_RETURN_ON_ERROR(ensure_directory("/data/weights"), TAG,
                      "failed to create weights dir");
  s_storage_ready = true;
  return ESP_OK;
}

static esp_err_t save_json_to_file(const char *path, cJSON *json) {
  if (!data_fs_lock(pdMS_TO_TICKS(2000))) {
    ESP_LOGE(TAG, "FS busy, cannot write %s", path);
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t err = ESP_OK;
  char *string = cJSON_PrintUnformatted(json);
  if (string == NULL) {
    ESP_LOGE(TAG, "Failed to print JSON");
    data_fs_unlock();
    return ESP_ERR_NO_MEM;
  }

  FILE *f = fopen(path, "w");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
    err = ESP_FAIL;
  } else {
    size_t written = fwrite(string, 1, strlen(string), f);
    if (written != strlen(string)) {
      ESP_LOGE(TAG, "Short write on %s", path);
      err = ESP_FAIL;
    }
    fclose(f);
  }

  free(string);
  data_fs_unlock();
  return err;
}

static cJSON *load_json_from_file(const char *path) {
  if (!data_fs_lock(pdMS_TO_TICKS(2000))) {
    ESP_LOGE(TAG, "FS busy, cannot read %s", path);
    return NULL;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for reading: %s", path);
    data_fs_unlock();
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (length <= 0 || length > CONFIG_ARS_DATA_MAX_JSON_SIZE) {
    ESP_LOGE(TAG, "Refusing to load %s (size=%ld)", path, length);
    fclose(f);
    data_fs_unlock();
    return NULL;
  }

  char *data = malloc(length + 1);
  if (data == NULL) {
    fclose(f);
    data_fs_unlock();
    return NULL;
  }

  size_t read_len = fread(data, 1, length, f);
  fclose(f);
  if (read_len != (size_t)length) {
    ESP_LOGE(TAG, "Short read on %s", path);
    free(data);
    data_fs_unlock();
    return NULL;
  }

  data[length] = '\0';

  cJSON *json = cJSON_Parse(data);
  free(data);
  data_fs_unlock();
  return json;
}

esp_err_t data_manager_save_reptile(const reptile_t *reptile) {
  if (!storage_ready_guard(__func__)) {
    return ESP_ERR_INVALID_STATE;
  }
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    ESP_LOGE(TAG, "Failed to allocate reptile object (id=%s)", reptile->id);
    return ESP_ERR_NO_MEM;
  }
  cJSON_AddStringToObject(root, "id", reptile->id);
  cJSON_AddStringToObject(root, "name", reptile->name);
  cJSON_AddStringToObject(root, "species", reptile->species);
  cJSON_AddStringToObject(root, "morph", reptile->morph);
  cJSON_AddNumberToObject(root, "birth_date", (double)reptile->birth_date);
  cJSON_AddNumberToObject(root, "gender", reptile->gender);
  cJSON_AddNumberToObject(root, "weight", reptile->weight);

  char path[128];
  snprintf(path, sizeof(path), "/data/reptiles/%s.json", reptile->id);
  esp_err_t err = save_json_to_file(path, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t data_manager_load_reptile(const char *id, reptile_t *out_reptile) {
  if (!storage_ready_guard(__func__)) {
    return ESP_ERR_INVALID_STATE;
  }

  char path[128];
  snprintf(path, sizeof(path), "/data/reptiles/%s.json", id);
  cJSON *json = load_json_from_file(path);
  if (json == NULL)
    return ESP_FAIL;

  cJSON *item;
  if ((item = cJSON_GetObjectItem(json, "id")))
    copy_bounded(out_reptile->id, sizeof(out_reptile->id), item->valuestring);
  if ((item = cJSON_GetObjectItem(json, "name")))
    copy_bounded(out_reptile->name, sizeof(out_reptile->name),
                 item->valuestring);
  if ((item = cJSON_GetObjectItem(json, "species")))
    copy_bounded(out_reptile->species, sizeof(out_reptile->species),
                 item->valuestring);
  if ((item = cJSON_GetObjectItem(json, "morph")))
    copy_bounded(out_reptile->morph, sizeof(out_reptile->morph),
                 item->valuestring);
  if ((item = cJSON_GetObjectItem(json, "birth_date")))
    out_reptile->birth_date = (int64_t)item->valuedouble;
  if ((item = cJSON_GetObjectItem(json, "gender")))
    out_reptile->gender = (reptile_gender_t)item->valueint;
  if ((item = cJSON_GetObjectItem(json, "weight")))
    out_reptile->weight = (float)item->valuedouble;

  cJSON_Delete(json);
  return ESP_OK;
}

esp_err_t data_manager_delete_reptile(const char *id) {
  if (!storage_ready_guard(__func__)) {
    return ESP_ERR_INVALID_STATE;
  }
  char path[128];
  snprintf(path, sizeof(path), "/data/reptiles/%s.json", id);
  if (!data_fs_lock(pdMS_TO_TICKS(2000))) {
    ESP_LOGE(TAG, "FS busy, cannot delete %s", path);
    return ESP_ERR_TIMEOUT;
  }
  int res = unlink(path);
  data_fs_unlock();
  return (res == 0) ? ESP_OK : ESP_FAIL;
}

cJSON *data_manager_list_reptiles(void) {
  cJSON *arr = cJSON_CreateArray();
  if (!arr) {
    ESP_LOGE(TAG, "Failed to allocate reptiles array");
    return NULL;
  }
  if (!storage_ready_guard(__func__)) {
    return arr;
  }
  if (!data_fs_lock(pdMS_TO_TICKS(2000))) {
    ESP_LOGE(TAG, "FS busy, cannot list reptiles");
    return arr;
  }
  DIR *d;
  struct dirent *dir;
  d = opendir("/data/reptiles");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (strstr(dir->d_name, ".json")) {
        char id[MAX_ID_LEN];
        copy_bounded(id, sizeof(id), dir->d_name);
        char *ext = strstr(id, ".json");
        if (ext)
          *ext = '\0';

        reptile_t r;
        if (data_manager_load_reptile(id, &r) == ESP_OK) {
          cJSON *obj = cJSON_CreateObject();
          if (!obj) {
            ESP_LOGE(TAG, "Failed to allocate reptile entry for %s", dir->d_name);
            closedir(d);
            cJSON_Delete(arr);
            return NULL;
          }
          cJSON_AddStringToObject(obj, "id", r.id);
          cJSON_AddStringToObject(obj, "name", r.name);
          cJSON_AddItemToArray(arr, obj);
        }
      }
    }
    closedir(d);
  }
  data_fs_unlock();
  return arr;
}

esp_err_t data_manager_add_event(const reptile_event_t *event) {
  if (!storage_ready_guard(__func__)) {
    return ESP_ERR_INVALID_STATE;
  }
  char path[128];
  snprintf(path, sizeof(path), "/data/events/%s.json", event->reptile_id);

  cJSON *root = load_json_from_file(path);
  if (root == NULL) {
    root = cJSON_CreateArray();
  }
  if (!root) {
    ESP_LOGE(TAG, "Failed to allocate events array for %s", event->reptile_id);
    return ESP_ERR_NO_MEM;
  }

  cJSON *evt_obj = cJSON_CreateObject();
  if (!evt_obj) {
    ESP_LOGE(TAG, "Failed to allocate event object for %s", event->id);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  cJSON_AddStringToObject(evt_obj, "id", event->id);
  cJSON_AddStringToObject(evt_obj, "reptile_id", event->reptile_id);
  cJSON_AddNumberToObject(evt_obj, "type", event->type);
  cJSON_AddNumberToObject(evt_obj, "timestamp", (double)event->timestamp);
  cJSON_AddStringToObject(evt_obj, "notes", event->notes);

  cJSON_AddItemToArray(root, evt_obj);

  esp_err_t err = save_json_to_file(path, root);
  cJSON_Delete(root);
  return err;
}

cJSON *data_manager_get_events(const char *reptile_id) {
  if (!storage_ready_guard(__func__)) {
    return NULL;
  }
  char path[128];
  snprintf(path, sizeof(path), "/data/events/%s.json", reptile_id);
  cJSON *json = load_json_from_file(path);
  if (json == NULL) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
      ESP_LOGE(TAG, "Failed to allocate events array for %s", reptile_id);
    }
    return arr;
  }
  return json;
}

esp_err_t data_manager_add_weight(const char *reptile_id, float weight,
                                  int64_t timestamp) {
  if (!storage_ready_guard(__func__)) {
    return ESP_ERR_INVALID_STATE;
  }
  char path[128];
  snprintf(path, sizeof(path), "/data/weights/%s.json", reptile_id);

  cJSON *root = load_json_from_file(path);
  if (root == NULL) {
    root = cJSON_CreateArray();
  }
  if (!root) {
    ESP_LOGE(TAG, "Failed to allocate weights array for %s", reptile_id);
    return ESP_ERR_NO_MEM;
  }

  cJSON *w_obj = cJSON_CreateObject();
  if (!w_obj) {
    ESP_LOGE(TAG, "Failed to allocate weight entry for %s", reptile_id);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  cJSON_AddNumberToObject(w_obj, "weight", weight);
  cJSON_AddNumberToObject(w_obj, "timestamp", (double)timestamp);

  cJSON_AddItemToArray(root, w_obj);

  esp_err_t err = save_json_to_file(path, root);
  cJSON_Delete(root);
  return err;
}

cJSON *data_manager_get_weights(const char *reptile_id) {
  if (!storage_ready_guard(__func__)) {
    return NULL;
  }
  char path[128];
  snprintf(path, sizeof(path), "/data/weights/%s.json", reptile_id);
  cJSON *json = load_json_from_file(path);
  if (json == NULL) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
      ESP_LOGE(TAG, "Failed to allocate weights array for %s", reptile_id);
    }
    return arr;
  }
  return json;
}

const char *gender_to_str(reptile_gender_t gender) {
  switch (gender) {
  case GENDER_MALE:
    return "Male";
  case GENDER_FEMALE:
    return "Female";
  default:
    return "Unknown";
  }
}
