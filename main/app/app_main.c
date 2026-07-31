#include "nvs_flash.h"

#include "app_ui.h"
#include "ble_scanner.h"
#include "csv_logger.h"
#include "device_manager.h"
#include "gateway_config.h"
#include "gateway_publisher.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "storage_manager.h"
#include "stress_console.h"
#include "gateway_status_console.h"
#include "system_diagnostics.h"
#include "time_service.h"

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    gateway_config_init();
    gateway_config_console_start();
    system_diagnostics_register_console();
    gateway_status_console_register();
    stress_console_register();
    time_service_init();
    ble_scanner_init();
    device_manager_init();
    storage_manager_start();
    csv_logger_start();
    app_ui_start();
    network_manager_start();
    mqtt_service_start();
    gateway_publisher_start();
}
