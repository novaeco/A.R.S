#include "touch_transform.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "touch_orient.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <string.h>

#define TOUCHCAL_MAGIC TOUCH_TRANSFORM_MAGIC
#define TOUCHCAL_VERSION TOUCH_TRANSFORM_VERSION
#define TOUCHCAL_NS "touchcal"
#define TOUCHCAL_SLOT_A "slotA"
#define TOUCHCAL_SLOT_B "slotB"

static const char *TAG = "touch_tf_store";
static uint32_t calc_crc(const touch_transform_record_t *rec);
static void sync_touch_orient_flags(const touch_transform_t *tf);

#ifndef CONFIG_ARS_TOUCH_SWAP_XY
#define CONFIG_ARS_TOUCH_SWAP_XY 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_X
#define CONFIG_ARS_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_ARS_TOUCH_MIRROR_Y
#define CONFIG_ARS_TOUCH_MIRROR_Y 0
#endif

static void touch_transform_set_defaults(touch_transform_record_t *rec) {
  if (!rec)
    return;
  memset(rec, 0, sizeof(*rec));
  touch_transform_identity(&rec->transform);
  rec->transform.swap_xy = CONFIG_ARS_TOUCH_SWAP_XY;
  rec->transform.mirror_x = CONFIG_ARS_TOUCH_MIRROR_X;
  rec->transform.mirror_y = CONFIG_ARS_TOUCH_MIRROR_Y;
  rec->magic = TOUCHCAL_MAGIC;
  rec->version = TOUCHCAL_VERSION;
  rec->generation = 0;
  rec->crc32 = calc_crc(rec);
}

__attribute__((weak)) esp_err_t touch_orient_load(touch_orient_config_t *cfg) {
  ESP_LOGW(TAG,
           "touch_orient_load weak stub invoked (component not linked); skipping "
           "legacy migration");
  if (cfg)
    touch_orient_get_defaults(cfg);
  return ESP_ERR_NOT_SUPPORTED;
}

__attribute__((weak)) void touch_orient_get_defaults(touch_orient_config_t *cfg) {
  if (!cfg)
    return;

  *cfg = (touch_orient_config_t){
      .magic = TOUCH_ORIENT_MAGIC,
      .version = TOUCH_ORIENT_VERSION,
      .swap_xy = false,
      .mirror_x = false,
      .mirror_y = false,
      .scale_x = 1.0f,
      .scale_y = 1.0f,
      .offset_x = 0,
      .offset_y = 0,
      .crc32 = 0,
  };

  cfg->crc32 = esp_crc32_le(0, (const uint8_t *)cfg,
                             sizeof(*cfg) - sizeof(cfg->crc32));
}

static uint32_t calc_crc(const touch_transform_record_t *rec) {
  size_t payload_len = sizeof(touch_transform_record_t) - sizeof(uint32_t);
  return esp_crc32_le(0, (const uint8_t *)rec, payload_len);
}

static bool nvs_read_record(nvs_handle_t h, const char *key,
                            touch_transform_record_t *out) {
  size_t len = sizeof(*out);
  esp_err_t err = nvs_get_blob(h, key, out, &len);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGD(TAG, "Slot %s not found", key);
    return false;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Slot %s read error: %s", key, esp_err_to_name(err));
    return false;
  }
  if (len != sizeof(*out)) {
    ESP_LOGW(TAG, "Slot %s size mismatch (got %zu)", key, len);
    return false;
  }
  if (out->magic != TOUCHCAL_MAGIC) {
    ESP_LOGW(TAG, "Slot %s invalid magic 0x%08" PRIx32, key, out->magic);
    return false;
  }
  if (out->version != TOUCHCAL_VERSION) {
    ESP_LOGW(TAG, "Slot %s invalid version %u", key, out->version);
    return false;
  }
  uint32_t crc_calc = calc_crc(out);
  if (crc_calc != out->crc32) {
    ESP_LOGE(TAG, "Slot %s CRC mismatch calc=0x%08" PRIx32 " stored=0x%08" PRIx32,
             key, crc_calc, out->crc32);
    return false;
  }

  ESP_LOGI(TAG,
           "Slot %s OK gen=%" PRIu32 " v%u crc=0x%08" PRIx32
           " swap=%d mirX=%d mirY=%d",
           key, out->generation, out->version, out->crc32,
           out->transform.swap_xy, out->transform.mirror_x,
           out->transform.mirror_y);
  return true;
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
  if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
    ESP_LOGW(TAG, "NVS not initialized; initializing now");
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init());
    err = nvs_open(TOUCHCAL_NS, NVS_READONLY, &h);
  }
  if (err != ESP_OK) {
    touch_transform_set_defaults(out);
    return err;
  }

  touch_transform_record_t slot_a, slot_b;
  bool has_a = nvs_read_record(h, TOUCHCAL_SLOT_A, &slot_a);
  bool has_b = nvs_read_record(h, TOUCHCAL_SLOT_B, &slot_b);
  nvs_close(h);

  const touch_transform_record_t *winner = NULL;
  const char *winner_key = NULL;
  choose_slots(has_a ? &slot_a : NULL, has_b ? &slot_b : NULL, &winner,
               &winner_key);
  if (!winner) {
    ESP_LOGW(TAG, "No valid touch transform slots found in NVS");
    touch_transform_set_defaults(out);
    return ESP_ERR_NOT_FOUND;
  }

  *out = *winner;
  ESP_LOGI(TAG,
           "Loaded transform from %s gen=%" PRIu32
           " swap=%d mirX=%d mirY=%d a=[[%.4f %.4f %.2f];[%.4f %.4f %.2f]]",
           winner_key, winner->generation, winner->transform.swap_xy,
           winner->transform.mirror_x, winner->transform.mirror_y,
           (double)winner->transform.a11, (double)winner->transform.a12,
           (double)winner->transform.a13, (double)winner->transform.a21,
           (double)winner->transform.a22, (double)winner->transform.a23);
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
  bool has_a = nvs_read_record(h, TOUCHCAL_SLOT_A, &slot_a);
  bool has_b = nvs_read_record(h, TOUCHCAL_SLOT_B, &slot_b);
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
    sync_touch_orient_flags(&to_store.transform);
    ESP_LOGI(TAG,
             "Stored transform into %s gen=%" PRIu32
             " crc=0x%08" PRIx32 " swap=%d mirX=%d mirY=%d",
             target_key, to_store.generation, to_store.crc32,
             to_store.transform.swap_xy, to_store.transform.mirror_x,
             to_store.transform.mirror_y);
  } else {
    ESP_LOGE(TAG, "Failed to save transform: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t touch_transform_storage_clear(void) {
  nvs_handle_t h;
  esp_err_t err = open_rw(&h);
  if (err != ESP_OK)
    return err;

  err = nvs_erase_all(h);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Cleared touchcal namespace (slots A/B)");
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

static void sync_touch_orient_flags(const touch_transform_t *tf) {
  if (!tf)
    return;

  touch_orient_config_t orient_cfg;
  if (touch_orient_load(&orient_cfg) != ESP_OK) {
    touch_orient_get_defaults(&orient_cfg);
  }

  orient_cfg.swap_xy = tf->swap_xy;
  orient_cfg.mirror_x = tf->mirror_x;
  orient_cfg.mirror_y = tf->mirror_y;
  orient_cfg.scale_x = 1.0f;
  orient_cfg.scale_y = 1.0f;
  orient_cfg.offset_x = 0;
  orient_cfg.offset_y = 0;

  esp_err_t err = touch_orient_save(&orient_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to sync touch_orient flags: %s", esp_err_to_name(err));
  }
}

