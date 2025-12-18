#include "touch_transform.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "touch_orient.h"
#include <inttypes.h>
#include <string.h>

#define TOUCHCAL_MAGIC TOUCH_TRANSFORM_MAGIC
#define TOUCHCAL_VERSION TOUCH_TRANSFORM_VERSION
#define TOUCHCAL_NS "touchcal"
#define TOUCHCAL_SLOT_A "slotA"
#define TOUCHCAL_SLOT_B "slotB"

static const char *TAG = "touch_tf_store";

static uint32_t calc_crc(const touch_transform_record_t *rec) {
  size_t payload_len = sizeof(touch_transform_record_t) - sizeof(uint32_t);
  return esp_crc32_le(0, (const uint8_t *)rec, payload_len);
}

static esp_err_t nvs_read_record(nvs_handle_t h, const char *key,
                                 touch_transform_record_t *out) {
  size_t len = sizeof(*out);
  esp_err_t err = nvs_get_blob(h, key, out, &len);
  if (err != ESP_OK)
    return err;
  if (len != sizeof(*out))
    return ESP_ERR_INVALID_SIZE;
  if (out->magic != TOUCHCAL_MAGIC)
    return ESP_ERR_INVALID_STATE;
  if (out->version != TOUCHCAL_VERSION)
    return ESP_ERR_INVALID_VERSION;
  if (calc_crc(out) != out->crc32)
    return ESP_ERR_INVALID_CRC;
  return ESP_OK;
}

static esp_err_t nvs_write_record(nvs_handle_t h, const char *key,
                                  touch_transform_record_t *rec) {
  rec->crc32 = calc_crc(rec);
  esp_err_t err = nvs_set_blob(h, key, rec, sizeof(*rec));
  if (err != ESP_OK)
    return err;
  return nvs_commit(h);
}

static void choose_slots(const touch_transform_record_t *a,
                         const touch_transform_record_t *b,
                         const touch_transform_record_t **winner,
                         const char **winner_key) {
  *winner = NULL;
  *winner_key = NULL;
  if (a && b) {
    if (a->generation >= b->generation) {
      *winner = a;
      *winner_key = TOUCHCAL_SLOT_A;
    } else {
      *winner = b;
      *winner_key = TOUCHCAL_SLOT_B;
    }
  } else if (a) {
    *winner = a;
    *winner_key = TOUCHCAL_SLOT_A;
  } else if (b) {
    *winner = b;
    *winner_key = TOUCHCAL_SLOT_B;
  }
}

esp_err_t touch_transform_storage_load(touch_transform_record_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t h;
  esp_err_t err = nvs_open(TOUCHCAL_NS, NVS_READONLY, &h);
  if (err != ESP_OK) {
    return err;
  }

  touch_transform_record_t slot_a, slot_b;
  bool has_a = nvs_read_record(h, TOUCHCAL_SLOT_A, &slot_a) == ESP_OK;
  bool has_b = nvs_read_record(h, TOUCHCAL_SLOT_B, &slot_b) == ESP_OK;
  nvs_close(h);

  const touch_transform_record_t *winner = NULL;
  const char *winner_key = NULL;
  choose_slots(has_a ? &slot_a : NULL, has_b ? &slot_b : NULL, &winner,
               &winner_key);
  if (!winner)
    return ESP_ERR_NOT_FOUND;

  *out = *winner;
  ESP_LOGI(TAG, "Loaded transform from %s gen=%" PRIu32 " v%u", winner_key,
           winner->generation, winner->version);
  return ESP_OK;
}

static esp_err_t open_rw(nvs_handle_t *out) {
  esp_err_t err = nvs_open(TOUCHCAL_NS, NVS_READWRITE, out);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = nvs_flash_init();
    if (err == ESP_OK || err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      nvs_flash_init();
    }
    err = nvs_open(TOUCHCAL_NS, NVS_READWRITE, out);
  }
  return err;
}

esp_err_t touch_transform_storage_save(const touch_transform_record_t *rec) {
  if (!rec)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t h;
  esp_err_t err = open_rw(&h);
  if (err != ESP_OK)
    return err;

  touch_transform_record_t slot_a, slot_b;
  bool has_a = nvs_read_record(h, TOUCHCAL_SLOT_A, &slot_a) == ESP_OK;
  bool has_b = nvs_read_record(h, TOUCHCAL_SLOT_B, &slot_b) == ESP_OK;
  uint32_t next_gen = 1;
  if (has_a)
    next_gen = slot_a.generation + 1;
  if (has_b && slot_b.generation >= next_gen)
    next_gen = slot_b.generation + 1;

  const char *target_key = (!has_a || (has_b && slot_b.generation < slot_a.generation))
                               ? TOUCHCAL_SLOT_B
                               : TOUCHCAL_SLOT_A;

  touch_transform_record_t to_store = *rec;
  to_store.magic = TOUCHCAL_MAGIC;
  to_store.version = TOUCHCAL_VERSION;
  to_store.generation = next_gen;
  to_store.crc32 = calc_crc(&to_store);

  err = nvs_write_record(h, target_key, &to_store);
  nvs_close(h);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Stored transform into %s gen=%" PRIu32, target_key,
             to_store.generation);
  } else {
    ESP_LOGE(TAG, "Failed to save transform: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t touch_transform_storage_migrate_old(touch_transform_record_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  touch_orient_config_t legacy;
  esp_err_t err = touch_orient_load(&legacy);
  if (err != ESP_OK)
    return err;

  touch_transform_identity(&out->transform);
  out->transform.swap_xy = legacy.swap_xy;
  out->transform.mirror_x = legacy.mirror_x;
  out->transform.mirror_y = legacy.mirror_y;
  out->transform.a11 = legacy.scale_x;
  out->transform.a22 = legacy.scale_y;
  out->transform.a13 = (float)legacy.offset_x;
  out->transform.a23 = (float)legacy.offset_y;
  out->magic = TOUCHCAL_MAGIC;
  out->version = TOUCHCAL_VERSION;
  out->generation = 1;
  out->crc32 = calc_crc(out);
  ESP_LOGW(TAG, "Migrated legacy touch_orient to touchcal namespace");
  return touch_transform_storage_save(out);
}

