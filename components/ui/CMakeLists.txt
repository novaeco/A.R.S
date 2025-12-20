file(GLOB_RECURSE UI_SRCS "ui.c" "ui_calibration.c" "*.c")

idf_component_register(SRCS ${UI_SRCS}
    INCLUDE_DIRS "." "screens"
                    REQUIRES lvgl touch_orient touch_transform
    PRIV_REQUIRES core_service reptile_storage iot net board lvgl_port)
