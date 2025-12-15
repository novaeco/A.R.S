#include "ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"
#include "ui_navigation.h"
#include "ui_screen_manager.h"
#include "ui_theme.h"
#include "ui_wizard.h"
#include <stdbool.h>

static const char *TAG = "ui";

static bool ui_is_setup_done(void) {
  nvs_handle_t handle;
  uint8_t done = 0;

  esp_err_t err = nvs_open("system", NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "Setup flag namespace not found in NVS (first boot assumed)");
    // Create namespace to avoid repeating the info log on next boot
    if (nvs_open("system", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_close(handle);
    }
    return false;
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS open failed while checking setup flag: %s",
             esp_err_to_name(err));
    return false;
  }

  err = nvs_get_u8(handle, "setup_done", &done);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "Setup flag not set yet (first boot)");
  } else if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to read setup_done flag from NVS: %s",
             esp_err_to_name(err));
  }
  nvs_close(handle);

  return done == 1;
}

void ui_init(void) {
  ESP_LOGI(TAG, "Initializing UI orchestration...");

  lv_display_t *display = lv_display_get_default();
  if (!display) {
    ESP_LOGE(TAG, "No default display found; aborting UI init");
    return;
  }

  ui_theme_init();
  ui_screen_manager_init();
  ui_nav_init();

  if (ui_is_setup_done()) {
    ESP_LOGI(TAG, "Setup already completed -> loading dashboard");
    ui_nav_navigate(UI_SCREEN_DASHBOARD, false);
  } else {
    ESP_LOGI(TAG, "Setup pending -> starting wizard flow");
    ui_wizard_start();
  }
}
