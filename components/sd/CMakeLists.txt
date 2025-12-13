idf_component_register(SRCS "sd.c"
                        INCLUDE_DIRS "."
                        REQUIRES fatfs vfs sdmmc esp_driver_sdspi esp_driver_spi io_extension driver
                    )
