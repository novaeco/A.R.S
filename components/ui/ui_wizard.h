#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_wizard_start(void);
void ui_wizard_next(void);
bool ui_wizard_handle_wifi_cancel(void);
bool ui_wizard_is_running(void);

#ifdef __cplusplus
}
#endif
