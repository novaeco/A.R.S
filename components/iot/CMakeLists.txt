idf_component_register(
    SRCS "src/iot_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi wpa_supplicant nvs_flash mqtt esp_event esp_https_ota
                  esp_http_client)
