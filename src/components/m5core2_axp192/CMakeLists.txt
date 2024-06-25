idf_component_register(
    SRCS "m5core2_axp192.c"
    INCLUDE_DIRS "."
    REQUIRES axp192 esp_i2c_manager
)
