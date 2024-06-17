idf_component_register(SRCS "env_config.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_common esp_wifi nvs_flash)