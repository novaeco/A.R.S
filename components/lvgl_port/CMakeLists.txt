idf_component_register(SRCS "lvgl_port.c" 
                        INCLUDE_DIRS "."
                        REQUIRES esp_driver_gpio esp_driver_i2c esp_timer esp_lcd i2c gpio io_extension rgb_lcd_port touch touch_transform lvgl
                        PRIV_REQUIRES ui
                    )

    add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)