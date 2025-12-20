#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the embedded web server.
 * 
 * @return esp_err_t 
 */
esp_err_t web_server_init(void);

/**
 * @brief Stop the web server.
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif