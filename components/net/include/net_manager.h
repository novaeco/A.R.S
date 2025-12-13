#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the network manager (WiFi, SNTP).
 *
 * @return esp_err_t
 */
esp_err_t net_init(void);

/**
 * @brief Connect to a WiFi network.
 *
 * @param ssid SSID of the network.
 * @param password Password of the network.
 * @return esp_err_t
 */
esp_err_t net_connect(const char *ssid, const char *password);

/**
 * @brief Check if connected to WiFi and got IP.
 *
 * @return true
 * @return false
 */

typedef struct {
  bool is_connected;
  bool got_ip;
  int8_t rssi;
  char ip_addr[16];
  esp_err_t last_error;
} net_status_t;

/**
 * @brief Get the current network status.
 *
 * @return net_status_t
 */
net_status_t net_get_status(void);

/**
 * @brief Check if connected to WiFi and got IP.
 *
 * @return true
 * @return false
 */
bool net_is_connected(void);

/**
 * @brief Perform a simple HTTP GET request.
 *
 * @param url Target URL.
 * @param out_buffer Buffer to store the response body.
 * @param buffer_len Size of the buffer.
 * @return esp_err_t
 */
esp_err_t net_http_get(const char *url, char *out_buffer, size_t buffer_len);

/**
 * @brief Provision Wi-Fi credentials.
 *
 * @param ssid SSID string (null-terminated).
 * @param password Password string (null-terminated).
 * @param persist If true, store credentials in NVS (namespace "net", keys
 * "wifi_ssid"/"wifi_pass").
 * @return esp_err_t ESP_OK on success; validation or storage errors otherwise.
 */
esp_err_t net_manager_set_credentials(const char *ssid, const char *password,
                                      bool persist);

/**
 * @brief Check if Wi-Fi credentials are set.
 *
 * @return true if credentials are set, false otherwise.
 */
bool net_manager_is_provisioned(void);

#ifdef __cplusplus
}
#endif
