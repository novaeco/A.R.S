idf_component_register(SRCS "touch_gt911.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver i2c_bus_shared io_extension board)
