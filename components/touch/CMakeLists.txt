
idf_component_register(SRCS "gt911.c" "touch.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_lcd i2c gpio io_extension esp_driver_gpio esp_driver_i2c esp_lcd_touch
                        PRIV_REQUIRES esp_timer
                    )
