idf_component_register(SRCS "storage_core.c"
                    INCLUDE_DIRS "."
                    REQUIRES fatfs cjson sd nvs_flash)
