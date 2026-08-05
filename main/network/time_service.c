#include "time_service.h"

#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "gateway_config.h"
#include "time_service_core.h"

static const char *TAG = "time_service";
static volatile bool time_synced;
static bool sntp_started;
static uint64_t sync_wall_ms;

static void time_sync_callback(struct timeval *timeval)
{
    (void)timeval;
    sync_wall_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
    time_synced = true;
    ESP_LOGI(TAG, "SNTP time synchronized");
}

void time_service_init(void)
{
    const gateway_config_t *config = gateway_config_get();
    setenv("TZ", config->timezone[0] ? config->timezone : "CST-8", 1);
    tzset();
}

void time_service_start_sync(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    const gateway_config_t *gateway = gateway_config_get();
    if (sntp_started) return;
    config.servers[0] = gateway->ntp_server[0] ? gateway->ntp_server : "pool.ntp.org";
    config.sync_cb = time_sync_callback;
    if (esp_netif_sntp_init(&config) == ESP_OK) {
        esp_netif_sntp_start();
        sntp_started = true;
        ESP_LOGI(TAG, "SNTP started: %s", gateway->ntp_server);
    }
}

bool time_service_is_synced(void) { return time_synced; }
const char *time_service_status_text(void) { return time_synced ? "Synced" : "Waiting"; }

bool time_service_format_wall_ms(uint64_t wall_ms, char *output, size_t output_size)
{
    struct timeval now;
    struct tm local_time;
    int64_t event_ms;
    uint64_t current_wall_ms;
    if (!time_synced || output == NULL || output_size == 0 ||
        !time_service_event_is_after_sync(wall_ms, sync_wall_ms)) {
        return false;
    }
    gettimeofday(&now, NULL);
    current_wall_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
    event_ms = (int64_t)now.tv_sec * 1000LL + now.tv_usec / 1000LL -
               time_service_elapsed_ms(current_wall_ms, wall_ms);
    time_t seconds = (time_t)(event_ms / 1000LL);
    localtime_r(&seconds, &local_time);
    return strftime(output, output_size, "%Y-%m-%dT%H:%M:%S%z", &local_time) != 0;
}
