#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

lv_obj_t *ui_create_dashboard(void);
void ui_dashboard_on_enter(void);
void ui_dashboard_on_leave(void);
void ui_dashboard_cleanup(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // UI_DASHBOARD_H
