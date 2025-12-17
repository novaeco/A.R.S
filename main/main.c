
#include "net_manager.h"
#include "web_server.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "board.h"
#include "data_manager.h"
#include "iot_manager.h"
#include "lvgl_port.h" // For lock/unlock
#include "ui.h"
// #include "ui_calibration.h"
#include "sd.h"
#include "ui_wizard.h"
#include <stdbool.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

void app_main(void) {
  ESP_LOGI(TAG, "Starting Reptiles Assistant");

  bool display_ok = false;
  bool touch_ok = false;
  bool lvgl_ok = false;

  // 0. System Initialization (Critical: NVS & Network before anything else)
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize Network Infrastructure
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  // Create default STA netif and check for success
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (sta_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
  }

  // 1. Filesystem
  ESP_ERROR_CHECK(data_manager_init());

  // 2. Display Hardware (BSP Init)
  // Dependency Injection for UI to avoid circular Board dependency
  ui_set_battery_cb(board_get_battery_level);

  // This initializes LCD, Touch, SD, and starts the LVGL task.
  if (app_board_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init Board");
    // If display fails, we might maintain standard execution for debug
  } else {
    display_ok = app_board_get_panel_handle() != NULL;
    touch_ok = app_board_get_touch_handle() != NULL;
    // Init LGL Port (Moved from board.c)
    if (lvgl_port_init(app_board_get_panel_handle(),
                       app_board_get_touch_handle()) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to init LVGL Port");
    } else {
      lvgl_ok = true;
    }
  }

  // 3. UI & Application Flow
  // 3. UI & Application Flow
  // Moved to LVGL task in lvgl_port.c to prevent WDT on Main Task
  ESP_LOGI(TAG, "UI creation delegated to LVGL task");

  // 4. Initialize SD Card (Async-like, after UI is up)
  // This ensures a missing SD card doesn't block the UI from showing
  // ARS: Swapped to dedicated Waveshare component fix
  esp_err_t sd_ret = sd_card_init();
  if (sd_ret != ESP_OK) {
    ESP_LOGW(TAG, "SD Card mounting failed or card not present (state=%s)",
             sd_state_str(sd_get_state()));
  } else {
    ESP_LOGI(TAG, "SD Card state: %s", sd_state_str(sd_get_state()));
  }

  // 5. WiFi / Web Server Init
  // net_init() is safe to call, it will auto-connect if credentials exist in
  // NVS from a previous setup or manual config.
  net_init();

  net_status_t net_status = net_get_status();
  bool wifi_provisioned = net_manager_is_provisioned();
  const char *wifi_state = wifi_provisioned
                               ? (net_status.is_connected ? "provisioned_connected"
                                                          : "provisioned_not_connected")
                               : "not_provisioned";

  ESP_LOGI(TAG,
           "BOOT-SUMMARY display=%s touch=%s lvgl=%s sd=%s wifi=%s",
           display_ok ? "ok" : "fail",
           touch_ok ? "ok" : "fail",
           lvgl_ok ? "ok" : "fail",
           sd_state_str(sd_get_state()), wifi_state);

  // --- Verification: Log Touch Coordinates for 10s ---
  // app_board_run_diagnostics(); // Removed to prevent blocking/interference

  ESP_LOGI(TAG, "Main task setup complete. Deleting main task.");
  vTaskDelete(NULL);
}
