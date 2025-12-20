#include "board.h"
#include "esp_log.h"

static const char *TAG = "board";

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "Board init stub: pins configured via board.h constants");
    return ESP_OK;
}
