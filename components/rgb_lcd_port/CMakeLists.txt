idf_build_get_property(idf_path IDF_PATH)
set(esp_lcd_rgb_include "${idf_path}/components/esp_lcd/rgb/include")

if(NOT EXISTS "${esp_lcd_rgb_include}")
    message(FATAL_ERROR "esp_lcd RGB include directory not found: ${esp_lcd_rgb_include}")
endif()

idf_component_register(SRCS "rgb_lcd_port.c"
                        INCLUDE_DIRS "." "${esp_lcd_rgb_include}"
                        REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd gpio i2c io_extension
                        )
