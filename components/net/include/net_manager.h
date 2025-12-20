#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi_types.h"
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

typedef enum {
  WIFI_PROV_STATE_NOT_PROVISIONED = 0,
  WIFI_PROV_STATE_CONNECTING,
  WIFI_PROV_STATE_CONNECTED,
  WIFI_PROV_STATE_FAILED,
  WIFI_PROV_STATE_CAPTIVE,
  WIFI_PROV_STATE_WRONG_PASSWORD,
} wifi_prov_state_t;

typedef enum {
  NET_MANAGER_EVENT_STATE_CHANGED = 0,
} net_manager_event_id_t;

typedef struct {
  wifi_prov_state_t state;
  wifi_err_reason_t reason;
} net_manager_state_evt_t;

ESP_EVENT_DECLARE_BASE(NET_MANAGER_EVENT);

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

wifi_prov_state_t net_manager_get_prov_state(void);

wifi_err_reason_t net_manager_get_last_reason(void);

/**
 * @brief Retrieve currently provisioned Wi-Fi credentials without logging
 *        sensitive data.
 *
 * The function prefers NVS-stored credentials when available and falls back to
 * the active Wi-Fi configuration (or Kconfig defaults) to keep the UI aligned
 * with the actual connection state.
 *
 * @param ssid Buffer to receive SSID (may be NULL). Must be large enough for
 *             32-byte SSID + terminator.
 * @param ssid_len Length of @p ssid buffer.
 * @param password Buffer to receive password (may be NULL). Must be large
 *                 enough for 63-byte password + terminator.
 * @param password_len Length of @p password buffer.
 * @return esp_err_t ESP_OK when credentials were retrieved, ESP_ERR_NOT_FOUND
 *                   if none are provisioned, or validation/storage errors.
 */
esp_err_t net_manager_get_credentials(char *ssid, size_t ssid_len,
                                      char *password, size_t password_len);

esp_err_t net_manager_forget_credentials(void);

#ifdef __cplusplus
}
#endif
