idf_component_register(SRCS "src/reptile_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_common log nvs_flash)
