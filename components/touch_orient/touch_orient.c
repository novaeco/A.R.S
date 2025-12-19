#include "touch_orient.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "touch.h"

static const char *TAG = "touch_orient";
#define NVS_NAMESPACE "touch"
#define NVS_KEY "orient"

static uint32_t calculate_crc(const touch_orient_config_t *cfg) {
  // CRC over magic, version, and flags (skip crc32 field at the end)
  const uint8_t *data = (const uint8_t *)cfg;
  size_t len = sizeof(touch_orient_config_t) - sizeof(uint32_t);
  return esp_crc32_le(0, data, len);
}

void touch_orient_get_defaults(touch_orient_config_t *cfg) {
  if (!cfg)
    return;
  cfg->magic = TOUCH_ORIENT_MAGIC;
  cfg->version = TOUCH_ORIENT_VERSION;
#ifdef CONFIG_ARS_TOUCH_SWAP_XY
  cfg->swap_xy = CONFIG_ARS_TOUCH_SWAP_XY;
#else
  cfg->swap_xy = false;
#endif
#ifdef CONFIG_ARS_TOUCH_MIRROR_X
  cfg->mirror_x = CONFIG_ARS_TOUCH_MIRROR_X;
#else
  cfg->mirror_x = false;
#endif
#ifdef CONFIG_ARS_TOUCH_MIRROR_Y
  cfg->mirror_y = CONFIG_ARS_TOUCH_MIRROR_Y;
#else
  cfg->mirror_y = false;
#endif
  cfg->scale_x = 1.0f;
  cfg->scale_y = 1.0f;
  cfg->offset_x = 0;
  cfg->offset_y = 0;
  cfg->crc32 = calculate_crc(cfg);
}

esp_err_t touch_orient_load(touch_orient_config_t *cfg) {
  if (!cfg)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  
  // Handling First Boot (Namespace not found) or Key Not Found
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "No NVS config found. Attempting to seed defaults.");
    
    // Try to open in RW mode to seed
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        touch_orient_get_defaults(cfg);
        
        // Save the defaults
        err = touch_orient_save(cfg);
        nvs_close(handle);
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Defaults seeded successfully.");
            return ESP_OK; // Return OK so the system proceeds normally
        } else {
            ESP_LOGE(TAG, "Failed to seed defaults: %s", esp_err_to_name(err));
            return err;
        }
    } else {
         ESP_LOGE(TAG, "Failed to open NVS in RW mode for seeding: %s", esp_err_to_name(err));
         // Fallback to defaults in memory only
         touch_orient_get_defaults(cfg);
         return ESP_OK; // Soft fail, use defaults
    }
  }
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return err;
  }

  size_t len = sizeof(touch_orient_config_t);
  touch_orient_config_t stored = {0};
  err = nvs_get_blob(handle, NVS_KEY, &stored, &len);
  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
      // Namespace exists but key missing? Seed it.
      ESP_LOGI(TAG, "Key not found. Seeding defaults.");
      touch_orient_get_defaults(cfg);
      touch_orient_save(cfg);
      return ESP_OK;
  }
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Config load failed: %s", esp_err_to_name(err));
    return err;
  }

  if (stored.magic != TOUCH_ORIENT_MAGIC) {
    ESP_LOGE(TAG, "Invalid magic");
    return ESP_ERR_INVALID_STATE;
  }

  // Upgrade path for legacy configs (version 1 had only orientation flags)
  if (len < sizeof(touch_orient_config_t) || stored.version < TOUCH_ORIENT_VERSION) {
    ESP_LOGW(TAG, "Upgrading touch config (len=%u, ver=%u)", (unsigned)len,
             (unsigned)stored.version);

    bool legacy_crc_ok = false;
    if (len >= sizeof(uint32_t) * 2 + sizeof(bool) * 3) {
      size_t crc_len = (len >= sizeof(uint32_t)) ? (len - sizeof(uint32_t)) : 0;
      uint32_t crc = esp_crc32_le(0, (const uint8_t *)&stored, crc_len);
      legacy_crc_ok = (crc == stored.crc32);
    }

    if (!legacy_crc_ok) {
      ESP_LOGW(TAG, "Legacy CRC mismatch; resetting to defaults");
      touch_orient_get_defaults(&stored);
    }
    // Inject defaults for new fields
    stored.scale_x = 1.0f;
    stored.scale_y = 1.0f;
    stored.offset_x = 0;
    stored.offset_y = 0;
    stored.version = TOUCH_ORIENT_VERSION;
    stored.crc32 = calculate_crc(&stored);
    touch_orient_save(&stored);
  } else {
    if (calculate_crc(&stored) != stored.crc32) {
      ESP_LOGE(TAG, "CRC mismatch");
      return ESP_ERR_INVALID_CRC;
    }
  }

  *cfg = stored;
  return ESP_OK;
}

esp_err_t touch_orient_save(const touch_orient_config_t *cfg) {
  if (!cfg)
    return ESP_ERR_INVALID_ARG;

  // Verify CRC before saving? Or just recalculate to be safe.
  // Let's recalculate to ensure integrity of what we save.
  touch_orient_config_t to_save = *cfg;
  to_save.magic = TOUCH_ORIENT_MAGIC;
  to_save.version = TOUCH_ORIENT_VERSION;
  to_save.crc32 = calculate_crc(&to_save);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_blob(handle, NVS_KEY, &to_save, sizeof(to_save));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Config saved: Swap=%d, MirX=%d, MirY=%d", to_save.swap_xy,
             to_save.mirror_x, to_save.mirror_y);
  } else {
    ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
  }

  return err;
}

esp_err_t touch_orient_apply(esp_lcd_touch_handle_t tp,
                             const touch_orient_config_t *cfg) {
  if (!tp || !cfg)
    return ESP_ERR_INVALID_ARG;

  esp_lcd_touch_set_swap_xy(tp, cfg->swap_xy);
  esp_lcd_touch_set_mirror_x(tp, cfg->mirror_x);
  esp_lcd_touch_set_mirror_y(tp, cfg->mirror_y);

  ars_touch_calibration_t cal = {
      .scale_x = (cfg->scale_x < 0.001f || cfg->scale_x > 100.0f)
                     ? 1.0f
                     : cfg->scale_x,
      .scale_y = (cfg->scale_y < 0.001f || cfg->scale_y > 100.0f)
                     ? 1.0f
                     : cfg->scale_y,
      .offset_x = cfg->offset_x,
      .offset_y = cfg->offset_y,
  };
  ars_touch_set_calibration(tp, &cal);

  ESP_LOGI(TAG,
           "Applied config: Swap=%d, MirX=%d, MirY=%d scale=(%.4f, %.4f) "
           "offset=(%d,%d)",
           cfg->swap_xy, cfg->mirror_x, cfg->mirror_y, (double)cal.scale_x,
           (double)cal.scale_y, cal.offset_x, cal.offset_y);
  return ESP_OK;
}

esp_err_t touch_orient_clear(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_erase_key(handle, NVS_KEY);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return ESP_ERR_NVS_NOT_FOUND;
  }

  esp_err_t commit_err = nvs_commit(handle);
  nvs_close(handle);

  if (commit_err != ESP_OK)
    return commit_err;

  ESP_LOGI(TAG, "Cleared touch orientation entry from NVS");
  return ESP_OK;
}
