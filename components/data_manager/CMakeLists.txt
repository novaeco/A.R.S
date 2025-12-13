idf_component_register(SRCS "src/data_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES espressif__cjson joltwallet__littlefs esp_common log freertos vfs)
