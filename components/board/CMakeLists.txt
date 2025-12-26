if(NOT BOOTLOADER_BUILD)
    idf_component_register(SRCS "src/board.c" "src/board_orientation.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_lcd lvgl touch
                        PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_common log freertos esp_partition fatfs
                                      io_extension sd rgb_lcd_port i2c gpio touch_orient touch_transform
                                      esp_adc esp_mm)
endif()
