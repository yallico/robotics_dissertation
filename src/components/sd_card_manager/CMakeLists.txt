idf_component_register(
                    SRCS "sd_card_manager.c"
                    INCLUDE_DIRS "." 
                    REQUIRES fatfs sdmmc https driver global_vars)