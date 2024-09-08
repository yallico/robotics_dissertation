idf_component_register(
    SRCS "data_logging.c"
    INCLUDE_DIRS "."
    REQUIRES global_vars json rtc_m5 sd_card_manager)
