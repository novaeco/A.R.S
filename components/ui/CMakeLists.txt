idf_component_register(SRCS "ui_app.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl lvgl_port domain_models compliance_rules documents export_share storage_core)
