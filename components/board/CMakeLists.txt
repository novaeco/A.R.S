idf_component_register(SRCS "board.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_spi i2c_bus_shared io_extension)
