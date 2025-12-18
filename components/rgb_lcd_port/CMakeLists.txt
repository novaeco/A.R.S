idf_component_register(SRCS "rgb_lcd_port.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd gpio i2c io_extension
                        )
