#pragma once
#include "esp_wifi.h"
#include "lvgl.h"

typedef enum {
  UI_WIFI_RESULT_SUCCESS = 0,
  UI_WIFI_RESULT_FAILED
} ui_wifi_result_t;

typedef void (*ui_wifi_result_cb_t)(ui_wifi_result_t result,
                                    wifi_err_reason_t reason);

#pragma once
#include "lvgl.h"

extern lv_obj_t *ui_ScreenWifi;

lv_obj_t *ui_create_screen_wifi(void);
void ui_wifi_on_enter(void);
void ui_wifi_on_leave(void);

/**
 * @brief Register a callback invoked when a Wi-Fi connection either succeeds
 * (got IP) or fails (disconnected with reason). Passing NULL clears the
 * callback.
 */
void ui_wifi_set_result_cb(ui_wifi_result_cb_t cb);
