idf_component_register(
    SRCS "rtc_m5.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_manager lwip
)
