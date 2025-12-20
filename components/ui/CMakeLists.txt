file(GLOB_RECURSE UI_SRCS "ui.c" "*.c")
list(FILTER UI_SRCS EXCLUDE REGEX ".*/ui_calibration\\.c$")

idf_component_register(SRCS ${UI_SRCS}
    INCLUDE_DIRS "." "screens"
                    REQUIRES lvgl touch_orient
    PRIV_REQUIRES core_service reptile_storage iot net board lvgl_port sd)
