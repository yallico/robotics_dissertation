idf_component_register(SRCS "gui_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl lvgl_esp32_drivers m5core2_axp192 rtc_m5 esp_timer esp_app_format global_vars genetic_algorithm)