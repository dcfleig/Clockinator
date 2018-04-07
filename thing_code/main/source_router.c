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
#include "freertos/queue.h"
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
#include "source_router.h"

static const char *TAG = "source_router";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern EventGroupHandle_t app_event_group;

static TaskHandle_t source_router_task_handle = NULL;

void start_source_router_task(void *param)
{
    if (source_router_task_handle == NULL)
    {
        ESP_LOGI(TAG, "Starting _source_router_task...");
        xTaskCreate(&_source_router_task, "_source_router_task", 9216, param, 5, &source_router_task_handle);
        if (source_router_task_handle == NULL)
        {
            ESP_LOGE(TAG, "_source_router_task creation failed");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Was going to start _source_router_task but it's already running");
    }
}

void stop_source_router_task()
{
    if (source_router_task_handle != NULL)
        vTaskDelete(source_router_task_handle);

    if (source_router_queue != NULL)
        vQueueDelete(source_router_queue);

    ESP_LOGI(TAG, "_source_router_task and source_router_queue deleted");
}

void _source_router_task(void *param)
{
    ESP_LOGI(TAG, "Creating source_router_queue");
    // Create a queue capable of containing 10 char[30] values.
    source_router_queue = xQueueCreate(4, sizeof(struct SourceRouterMessage));

    if (source_router_queue == NULL)
    {
        ESP_LOGE(TAG, "Error creating source_router_queue");
        abort();
    }

    ESP_LOGI(TAG, "Waiting for wifi to connect");
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, SNTP_CONNECTED_BIT | SHADOW_CONNECTED_BIT | SHADOW_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    while (1)
    {
        if (source_router_queue != 0)
        {
            _SourceRouterMessage message;
            // Receive a message on the created queue.  Block for 10 ticks if a
            // message is not immediately available.
            if (xQueueReceive(source_router_queue, &(message), (TickType_t)10))
            {
                ESP_LOGI(TAG, "Received SourceRouterMessage.");
                ESP_LOGI(TAG, "SourceRouterMessage bank: %s", message.bank == LEFT
                                                                  ? "LEFT"
                                                                  : "RIGHT");
                ESP_LOGI(TAG, "SourceRouterMessage data: %s", message.data);

                if ((strcasecmp(message.data, "*date") == 0) || (strcasecmp(message.data, "*time") == 0))
                {
                    if (strcasecmp(message.data, "*date") == 0)
                    {
                        ESP_LOGI(TAG, "Starting date_task on %s bank", message.bank == LEFT ? "LEFT" : "RIGHT");
                        ct_start_date_task(message.bank);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Starting time_task on %s bank", message.bank == LEFT ? "LEFT" : "RIGHT");
                        ct_start_time_task(message.bank);
                    }
                }
                else
                {
                    ct_stop_task(message.bank);
                }
            }
        }
        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}
