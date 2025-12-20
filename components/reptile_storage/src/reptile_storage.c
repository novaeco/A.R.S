#include "reptile_storage.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>


// static const char *TAG = "reptile_storage";

esp_err_t storage_nvs_set_str(const char *key, const char *value) {
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_str(my_handle, key, value);
  if (err == ESP_OK)
    err = nvs_commit(my_handle);

  nvs_close(my_handle);
  return err;
}

esp_err_t storage_nvs_get_str(const char *key, char *out_value,
                              size_t max_len) {
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return err;

  err = nvs_get_str(my_handle, key, out_value, &max_len);
  nvs_close(my_handle);
  return err;
}

esp_err_t storage_nvs_set_i32(const char *key, int32_t value) {
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_i32(my_handle, key, value);
  if (err == ESP_OK)
    err = nvs_commit(my_handle);

  nvs_close(my_handle);
  return err;
}

esp_err_t storage_nvs_get_i32(const char *key, int32_t *out_value) {
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return err;

  err = nvs_get_i32(my_handle, key, out_value);
  nvs_close(my_handle);
  return err;
}
