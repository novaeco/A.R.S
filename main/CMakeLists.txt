idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES board i2c_bus_shared io_extension touch touch_transform display lvgl_port sd storage_core domain_models compliance_rules documents export_share ui)
