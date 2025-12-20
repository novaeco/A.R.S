if(NOT BOOTLOADER_BUILD)
    idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES lvgl board data_manager ui iot lvgl_port net web_server sd)

    add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)

endif()
