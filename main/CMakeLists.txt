idf_component_register(SRCS "csv_formatter.c" "csv_logger.c" "gateway_config.c" "cores3_test.c" "app_ui.c" "ble_scanner.c" "device_filter.c" "device_manager.c" "device_manager_core.c" "device_list_model.c"
                       PRIV_REQUIRES bt nvs_flash m5stack_core_s3 esp_wifi esp_netif esp_event console
                       INCLUDE_DIRS "")
