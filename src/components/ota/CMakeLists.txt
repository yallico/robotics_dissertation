idf_component_register(SRCS "ota.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_https_ota app_update json gui_manager
                    EMBED_TXTFILES ../../server_certs/aws_root_ca.pem)