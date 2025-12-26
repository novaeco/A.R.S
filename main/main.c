
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
#include "rgb_lcd_port.h"
#include <esp_err.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "main";

static void log_main_stack_hwm(const char *stage) {
  UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(NULL);
  size_t hwm_bytes = hwm_words * sizeof(StackType_t);
  ESP_LOGI(TAG, "[stack] %s: HWM=%lu words (%u bytes)", stage,
           (unsigned long)hwm_words, (unsigned int)hwm_bytes);
}

void app_main(void) {
  esp_rom_printf("ARS: app_main reached (build=%s %s, idf=%s)\n", __DATE__,
                 __TIME__, esp_get_idf_version());
  ESP_LOGI(TAG, "app_main reached (build=%s %s, idf=%s)", __DATE__, __TIME__,
           esp_get_idf_version());
  ESP_LOGI(TAG, "Starting Reptiles Assistant");

  bool display_ok = false;
  bool touch_ok = false;
  bool lvgl_ok = false;
  bool storage_ok = false;
  bool base_net_stack_ok = false;

  log_main_stack_hwm("app_main entry");

  // Boot order guard rails:
  // 0) Persistent storage + base network stack (NVS, esp_netif, events) so
  //    that later services (Wi‑Fi/provisioning/Web) have their dependencies.
  // 1) Filesystem to allow early configuration/assets access.
  // 2) Board bring-up (panel + touch) before LVGL task so the UI can draw on a
  //    valid display object.
  // 3) SD card after UI to avoid blocking the boot UI if media is missing.
  // 4) Network/provisioning last because it depends on the previous steps.

  // 0. System Initialization (Critical: NVS & Network before anything else)
  // Initialize NVS
  rgb_lcd_port_pclk_guard_enter("nvs_init", NULL);
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS init needs erase: %s", esp_err_to_name(ret));
    esp_err_t erase_ret = nvs_flash_erase();
    if (erase_ret != ESP_OK) {
      ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(erase_ret));
    }
    ret = nvs_flash_init();
  }
  rgb_lcd_port_pclk_guard_exit();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s (continuing without NVS)", esp_err_to_name(ret));
  }

  esp_rom_printf("ARS: checkpoint 2 after NVS (idf=%s)\n", esp_get_idf_version());
  ESP_LOGI(TAG, "Checkpoint: NVS init complete, proceeding to board bring-up");
  log_main_stack_hwm("after NVS + base stack prep");

  // Initialize Network Infrastructure
  esp_err_t netif_ret = esp_netif_init();
  if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(netif_ret));
  }
  esp_err_t loop_ret = esp_event_loop_create_default();
  if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(loop_ret));
  }
  base_net_stack_ok = (ret == ESP_OK) &&
                      (netif_ret == ESP_OK || netif_ret == ESP_ERR_INVALID_STATE) &&
                      (loop_ret == ESP_OK || loop_ret == ESP_ERR_INVALID_STATE);

  // Create default STA netif and check for success
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (sta_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
  } else {
    base_net_stack_ok = base_net_stack_ok && true;
  }

  // 1. Filesystem
  esp_err_t storage_ret = data_manager_init();
  storage_ok = (storage_ret == ESP_OK) && data_manager_is_ready();
  if (!storage_ok) {
    ESP_LOGW(TAG, "Storage unavailable: %s", esp_err_to_name(storage_ret));
  }

  // 2. Display Hardware (BSP Init)
  // Dependency Injection for UI to avoid circular Board dependency
  ui_set_battery_cb(board_get_battery_level);
  // Ensure LVGL task invokes the real UI init instead of falling back to the
  // weak stub.
  lvgl_port_set_ui_init_cb(ui_init);

  // Yield before long init to give IDLE task a chance
  vTaskDelay(pdMS_TO_TICKS(10));

  // This initializes LCD and Touch.
  esp_err_t board_ret = app_board_init();
  if (board_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init Board: %s", esp_err_to_name(board_ret));
  } else {
    vTaskDelay(pdMS_TO_TICKS(50));
    display_ok = app_board_get_panel_handle() != NULL;
    touch_ok = app_board_get_touch_handle() != NULL;
  }

  if (display_ok || touch_ok) {
    log_main_stack_hwm("before LVGL port init");
    if (lvgl_port_init(app_board_get_panel_handle(),
                       app_board_get_touch_handle()) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to init LVGL Port");
    } else {
      lvgl_ok = true;
    }
    log_main_stack_hwm("after LVGL port init");
  } else {
    ESP_LOGW(TAG, "LVGL init skipped (display or touch missing)");
  }

  // 3. UI & Application Flow
  // 3. UI & Application Flow
  // Moved to LVGL task in lvgl_port.c to prevent WDT on Main Task
  ESP_LOGI(TAG, "UI creation delegated to LVGL task");

  esp_rom_printf("ARS: checkpoint 3 before LVGL/UI dispatch\n");
  ESP_LOGI(TAG, "Checkpoint: LVGL/UI dispatch about to run");
  log_main_stack_hwm("before UI dispatch");

  // 4. Initialize SD Card (Async-like, after UI is up)
  // This ensures a missing SD card doesn't block the UI from showing
  // ARS: Swapped to dedicated Waveshare component fix
  sd_state_t sd_state = SD_STATE_UNINITIALIZED;
  log_main_stack_hwm("before SD init");
  esp_err_t sd_ret = sd_card_init();
  sd_state = sd_get_state();
  log_main_stack_hwm("after SD init");
  if (sd_ret != ESP_OK) {
    ESP_LOGW(TAG, "SD Card mounting failed or card not present (state=%s)",
             sd_state_str(sd_state));
  } else {
    ESP_LOGI(TAG, "SD Card state: %s", sd_state_str(sd_state));
  }

  // 5. WiFi / Web Server Init
  // net_init() is safe to call, it will auto-connect if credentials exist in
  // NVS from a previous setup or manual config.
  vTaskDelay(pdMS_TO_TICKS(10)); // Yield to allow IDLE task to reset watchdog
  const char *wifi_state = "net_stack_unavailable";
  if (base_net_stack_ok) {
    net_init();

    vTaskDelay(pdMS_TO_TICKS(10)); // Yield after net_init for stability
    net_status_t net_status = net_get_status();
    bool wifi_provisioned = net_manager_is_provisioned();
    bool wifi_connected = net_status.is_connected;
    wifi_state = wifi_provisioned
                     ? (wifi_connected ? "provisioned_connected"
                                       : "provisioned_not_connected")
                     : "not_provisioned";
  } else {
    ESP_LOGW(TAG, "Network init skipped: base stack unavailable");
  }

  ESP_LOGI(TAG,
           "BOOT-SUMMARY storage=%s display=%s touch=%s lvgl=%s sd=%s wifi=%s",
           storage_ok ? "ok" : "unavailable", display_ok ? "ok" : "fail",
           touch_ok ? "ok" : "fail", lvgl_ok ? "ok" : "fail",
           sd_state_str(sd_state), wifi_state);

  // --- Verification: Log Touch Coordinates for 10s ---
  // app_board_run_diagnostics(); // Removed to prevent blocking/interference

  ESP_LOGI(TAG, "Main task setup complete. Deleting main task.");
  vTaskDelete(NULL);
}
