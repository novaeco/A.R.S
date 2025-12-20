idf_component_register(
    SRCS "src/net_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_http_client esp_http_server esp-tls esp_timer nvs_flash lwip espressif__cjson
    PRIV_REQUIRES core_service reptile_storage board web_server
)
