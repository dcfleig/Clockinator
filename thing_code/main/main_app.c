#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#include "display_manager.h"
#include "clock_tasks.h"
#include "shadow_task.h"

static const char *TAG = "main_app";

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t app_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int IP_CONNECTED_BIT = BIT0;
const int SNTP_CONNECTED_BIT = BIT1;
const int SHADOW_CONNECTED_BIT = BIT2;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(app_event_group, IP_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wifi connected.");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(app_event_group, IP_CONNECTED_BIT);
        ESP_LOGE(TAG, "Wifi disconnected.");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void startup_monitor_task(void *param)
{
    EventBits_t eventBits = 0;
    while (eventBits != (IP_CONNECTED_BIT | SHADOW_CONNECTED_BIT | SNTP_CONNECTED_BIT))
    {
        eventBits = xEventGroupWaitBits(app_event_group, IP_CONNECTED_BIT | SHADOW_CONNECTED_BIT | SNTP_CONNECTED_BIT,
                                        false, false, 0);
        ESP_LOGI(TAG, "Eventbits = %i", eventBits);
        char display[4] = "    ";
        display[1] = eventBits & IP_CONNECTED_BIT ? '.' : ' ';
        display[2] = eventBits & SHADOW_CONNECTED_BIT ? '.' : ' ';
        display[3] = eventBits & SNTP_CONNECTED_BIT ? '.' : ' ';
        dm_setLeftBank(display);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    ESP_LOGI(TAG, "Startup complete. Deleting startup_monitor_task.");
    dm_clear();
    vTaskDelete(NULL);
}

void logSystemInfo()
{
    ESP_LOGI(TAG, "esp-idf version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "free heap size: %d", esp_get_free_heap_size());
}

void app_main()
{
    app_event_group = xEventGroupCreate();

    TaskHandle_t startup_monitor_task_handle = NULL;

    dm_init();
    dm_clear();

    ESP_LOGI(TAG, "Starting startupMonitorTask...");
    xTaskCreate(&startup_monitor_task, "startup_monitor_task", 3000, NULL, 3, &startup_monitor_task_handle);
    if (startup_monitor_task_handle == NULL)
    {
        ESP_LOGE(TAG, "startup_monitor_task creation failed");
        abort();
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    initialise_wifi();

    start_shadow_task(NULL);
    start_source_router_task(NULL);
    tm_init();

    while (1)
    {
        logSystemInfo();
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
}
