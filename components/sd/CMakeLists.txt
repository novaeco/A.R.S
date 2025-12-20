idf_component_register(SRCS "sd_service.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver fatfs sdmmc board io_extension)
