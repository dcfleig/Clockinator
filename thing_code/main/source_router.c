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
#include "esp_task_wdt.h"

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
#include "topic_task.h"

static const char *TAG = "source_router";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern const int SHADOW_IN_PROGRESS_BIT;
extern const int MQTT_SEND_IN_PROGRESS_BIT;
extern const int SOURCE_ROUTER_STARTED_BIT;
extern EventGroupHandle_t app_event_group;

static TaskHandle_t source_router_task_handle = NULL;

void start_source_router_task(void *param)
{
    if (source_router_task_handle == NULL)
    {
        ESP_LOGI(TAG, "Starting _source_router_task...");
        xTaskCreate(&_source_router_task, "_source_router_task", SOURCE_ROUTER_TASKS_STACK_DEPTH, param, SOURCE_ROUTER_TASKS_PRIORITY, &source_router_task_handle);
        if (source_router_task_handle == NULL)
        {
            ESP_LOGE(TAG, "_source_router_task creation failed");
        }
        xEventGroupSetBits(app_event_group, SOURCE_ROUTER_STARTED_BIT);
    }
    else
    {
        ESP_LOGI(TAG, "Was going to start _source_router_task but it's already running");
    }
}

void stop_source_router_task()
{
    esp_task_wdt_delete(source_router_task_handle);

    if (source_router_task_handle != NULL)
        vTaskDelete(source_router_task_handle);

    if (source_router_queue != NULL)
        vQueueDelete(source_router_queue);

    xEventGroupClearBits(app_event_group, SOURCE_ROUTER_STARTED_BIT);
    ESP_LOGI(TAG, "_source_router_task and source_router_queue deleted");
}

void _source_router_task(void *param)
{
    //Subscribe this task to TWDT, then check if it is subscribed
    CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);

    ESP_LOGI(TAG, "Creating source_router_queue");
    // Create a queue capable of containing 10 char[30] values.
    source_router_queue = xQueueCreate(4, sizeof(struct SourceRouterMessage));

    if (source_router_queue == NULL)
    {
        ESP_LOGE(TAG, "Error creating source_router_queue");
        abort();
    }

    while (1)
    {
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK); //Comment this line to trigger a TWDT timeout
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
                ESP_LOGI(TAG, "SourceRouterMessage dataSource: %s", message.dataSource);

                if ((strcasecmp(message.dataSource, "*date") == 0) || (strcasecmp(message.dataSource, "*time") == 0))
                {
                    if (strcasecmp(message.dataSource, "*date") == 0)
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

                    if (strlen(message.dataSource) != 0 && message.dataSource[0] != ' ' && message.dataSource[0] != '$')
                    {
                        ESP_LOGI(TAG, "Setting %s bank topic to %s", message.bank == LEFT ? "LEFT" : "RIGHT", message.dataSource);
                        tt_bank_subscribe(message.bank, message.dataSource);
                    }
                }
            }
        }
        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}
