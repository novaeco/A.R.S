#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SETUP_NVS_NAMESPACE "ui"

void ui_wizard_start(void);
void ui_wizard_next(void);
bool ui_wizard_handle_wifi_cancel(void);
bool ui_wizard_is_running(void);
esp_err_t ui_wizard_mark_setup_done(void);
void ui_wizard_complete_from_calibration(void);

#ifdef __cplusplus
}
#endif
