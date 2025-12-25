/**
 * @file lv_conf.h
 * @brief LVGL Configuration for A.R.S Project
 *
 * Enabled assertions and logging for debugging "LoadProhibited" crash.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/*Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888)*/
#define LV_COLOR_DEPTH 16

/*====================
   MEMORY SETTINGS
 *====================*/

/*1: use custom malloc/free, 0: use the built-in `lv_mem_alloc/lv_mem_free`*/
#define LV_MEM_CUSTOM 0

/*====================
   HAL SETTINGS
 *====================*/

/*Defines the default tick source. 0: None, 1: Custom, 2: SysTick, 3: Arduino*/
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (esp_timer_get_time() / 1000)
#endif

/*====================
   FONT SETTINGS
 *====================*/

/* Enable Montserrat fonts needed by UI theme */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1

/*====================
   DEBUG SETTINGS
 *====================*/

/*Enable checks for assertion*/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1 /* Check validity of styles */
#define LV_USE_ASSERT_MEM_INTEGRITY 1
#define LV_USE_ASSERT_OBJ 1 /* Check validity of objects */

/*1: Enable logging*/
#define LV_USE_LOG 1
#if LV_USE_LOG
/*How important log should be added:
 *LV_LOG_LEVEL_TRACE       A lot of logs to give detailed information
 *LV_LOG_LEVEL_INFO        Log important events
 *LV_LOG_LEVEL_WARN        Log if something needs attention
 *LV_LOG_LEVEL_ERROR       Only critical errors*/
#define LV_LOG_LEVEL LV_LOG_LEVEL_INFO
#define LV_LOG_PRINTF 0
#define LV_LOG_TRACE_MEM 1
#define LV_LOG_TRACE_TIMER 1
#define LV_LOG_TRACE_INDEV 1
#define LV_LOG_TRACE_DISP_REFR 1
#define LV_LOG_TRACE_EVENT 1
#define LV_LOG_TRACE_OBJ_CREATE 1
#define LV_LOG_TRACE_LAYOUT 1
#define LV_LOG_TRACE_ANIM 1
#endif

#endif /*LV_CONF_H*/
