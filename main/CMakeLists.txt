idf_component_register(SRCS "cores3_test.c"
                       PRIV_REQUIRES spi_flash m5stack_core_s3 esp_wifi esp_netif esp_event nvs_flash
                       INCLUDE_DIRS "")
