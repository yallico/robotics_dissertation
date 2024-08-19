idf_component_register(SRCS "https.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_common json esp_http_client ota)