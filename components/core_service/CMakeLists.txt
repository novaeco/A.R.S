idf_component_register(SRCS "src/core_service.c" "core_service_alerts.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_common log data_manager reptile_storage)
