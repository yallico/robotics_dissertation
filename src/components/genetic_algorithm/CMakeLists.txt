idf_component_register(SRCS "ga.c"
                    INCLUDE_DIRS "."
                    REQUIRES https arduino global_vars data_logging espnow_main
                    EMBED_TXTFILES "../../server_certs/qrng_anu_ca.pem")