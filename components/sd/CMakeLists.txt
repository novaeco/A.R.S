idf_component_register(SRCS "sd_service.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_spi esp_driver_sdspi fatfs sdmmc board)
