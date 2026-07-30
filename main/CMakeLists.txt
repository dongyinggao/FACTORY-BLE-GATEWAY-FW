idf_component_register(SRCS "csv_formatter.c" "csv_logger.c" "gateway_config.c" "time_service.c" "network_manager.c" "mqtt_service.c" "outbox.c" "event_json.c" "gateway_publisher.c" "cores3_test.c" "app_ui.c" "ble_scanner.c" "device_filter.c" "device_manager.c" "device_manager_core.c"
                       PRIV_REQUIRES bt nvs_flash m5stack_core_s3 esp_wifi esp_netif esp_event console mqtt esp_timer
                       INCLUDE_DIRS "")
