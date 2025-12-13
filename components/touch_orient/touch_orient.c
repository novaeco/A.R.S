#include "touch_orient.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

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
  err = nvs_get_blob(handle, NVS_KEY, cfg, &len);
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

  if (len != sizeof(touch_orient_config_t)) {
    ESP_LOGE(TAG, "Config size mismatch");
    return ESP_ERR_INVALID_SIZE;
  }

  if (cfg->magic != TOUCH_ORIENT_MAGIC) {
    ESP_LOGE(TAG, "Invalid magic");
    return ESP_ERR_INVALID_STATE;
  }

  if (calculate_crc(cfg) != cfg->crc32) {
    ESP_LOGE(TAG, "CRC mismatch");
    return ESP_ERR_INVALID_CRC;
  }

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

  ESP_LOGI(TAG, "Applied config: Swap=%d, MirX=%d, MirY=%d", cfg->swap_xy,
           cfg->mirror_x, cfg->mirror_y);
  return ESP_OK;
}
