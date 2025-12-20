#include "display_driver.h"
#include "esp_log.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_check.h"
#include <stdbool.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel = NULL;
static volatile bool s_flush_active = false;

static bool lcd_vsync_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_data)
{
    (void)panel;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_data;
    if (s_flush_active) {
        s_flush_active = false;
        lv_display_flush_ready(disp);
    }
    return false;
}

static void fill_data_pins(int data_gpio_nums[16])
{
    data_gpio_nums[0] = BOARD_LCD_DATA0;
    data_gpio_nums[1] = BOARD_LCD_DATA1;
    data_gpio_nums[2] = BOARD_LCD_DATA2;
    data_gpio_nums[3] = BOARD_LCD_DATA3;
    data_gpio_nums[4] = BOARD_LCD_DATA4;
    data_gpio_nums[5] = BOARD_LCD_DATA5;
    data_gpio_nums[6] = BOARD_LCD_DATA6;
    data_gpio_nums[7] = BOARD_LCD_DATA7;
    data_gpio_nums[8] = BOARD_LCD_DATA8;
    data_gpio_nums[9] = BOARD_LCD_DATA9;
    data_gpio_nums[10] = BOARD_LCD_DATA10;
    data_gpio_nums[11] = BOARD_LCD_DATA11;
    data_gpio_nums[12] = BOARD_LCD_DATA12;
    data_gpio_nums[13] = BOARD_LCD_DATA13;
    data_gpio_nums[14] = BOARD_LCD_DATA14;
    data_gpio_nums[15] = BOARD_LCD_DATA15;
}

esp_err_t display_driver_init(void)
{
    if (s_panel) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Config RGB LCD %dx%d", BOARD_DISPLAY_H_RES, BOARD_DISPLAY_V_RES);

    int data_pins[16];
    fill_data_pins(data_pins);

    lcd_rgb_timing_t timings = {
        .pclk_hz = BOARD_DISPLAY_PCLK_HZ,
        .h_res = BOARD_DISPLAY_H_RES,
        .v_res = BOARD_DISPLAY_V_RES,
        .hsync_pulse_width = 10,
        .hsync_back_porch = 80,
        .hsync_front_porch = 40,
        .vsync_pulse_width = 10,
        .vsync_back_porch = 20,
        .vsync_front_porch = 20,
        .flags = {
            .pclk_active_neg = true,
        },
    };

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = timings,
        .data_width = 16,
        .bits_per_pixel = 16,
        .hsync_gpio_num = BOARD_LCD_HSYNC,
        .vsync_gpio_num = BOARD_LCD_VSYNC,
        .de_gpio_num = BOARD_LCD_DE,
        .pclk_gpio_num = BOARD_LCD_PCLK,
        .data_gpio_nums = {
            0,
        },
        .disp_gpio_num = BOARD_LCD_DISP_EN,
        .flags = {
            .fb_in_psram = 1,
            .double_fb = 0,
        },
        .bounce_buffer_size_px = BOARD_DISPLAY_H_RES * 20,
    };
    fill_data_pins(panel_config.data_gpio_nums);

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &s_panel), TAG, "panel new");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    ESP_LOGI(TAG, "RGB panel ready, enabling backlight");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_BACKLIGHT, 1), TAG, "backlight");
    return ESP_OK;
}

void display_driver_register_callbacks(lv_display_t *display)
{
    if (!s_panel || !display) {
        return;
    }
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = lcd_vsync_cb,
    };
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, display);
    lv_display_set_user_data(display, s_panel);
}

void display_driver_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(display);
    if (!panel) {
        lv_display_flush_ready(display);
        return;
    }
    s_flush_active = true;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}
