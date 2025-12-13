#include "ui.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui";

void ui_init(void) {
  ESP_LOGI(TAG, "Initializing UI...");
  ESP_LOGI(TAG, "UI: ui_init() (real UI) called");

  // Create the default theme
  lv_disp_t *dispp = lv_display_get_default();
  if (dispp) {
    lv_theme_t *theme = lv_theme_default_init(
        dispp, lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
  }

  // Create a simple screen to guarantee visibility
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_screen_load(scr);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "Reptiles Assistant");
  lv_obj_center(label);

  // Call the main screen creation function as declared in ui.h
  // ui_create_dashboard(); // Existing call preserved but ensuring simple label
  // is first

  ESP_LOGI(TAG, "UI Initialized (Basic Screen Loaded)");
}
