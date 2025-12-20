#pragma once
#include "esp_err.h"

typedef enum {
    SD_CARD_NOT_PRESENT = 0,
    SD_CARD_MOUNTED,
    SD_CARD_ERROR
} sd_card_state_t;

sd_card_state_t sd_service_init(void);
esp_err_t sd_service_get_mount_point(const char **path);
