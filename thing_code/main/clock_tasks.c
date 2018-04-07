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
#include "esp_log.h"

#include "display_manager.h"
#include "time_manager.h"
#include "clock_tasks.h"

static const char *TAG = "clock_tasks";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern EventGroupHandle_t app_event_group;

static TaskHandle_t task_handle_array[2];

static void _ct_time_task(dm_BANK_SELECT bank)
{
    ESP_LOGI(TAG, "Waiting for wifi to connect");
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, SNTP_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    while (1)
    {
        char display[5] = "";
        tm_getLocalTimeText("%2d.%02d", display);

        if (bank == RIGHT)
        {
            dm_setRightBank(display);
        }
        else
        {
            dm_setLeftBank(display);
        }
        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void _ct_start_task(dm_BANK_SELECT bank, TaskFunction_t task, char *taskName)
{
    if (task_handle_array[bank] != NULL)
    {
        ct_stop_task(bank);
    }
    ESP_LOGI(TAG, "starting task %s on the %s bank", taskName, bank == LEFT ? "LEFT" : "RIGHT");
    xTaskCreate(task, taskName, CLOCK_TASKS_STACK_DEPTH, (void *)bank, CLOCK_TASKS_PRIORITY, &task_handle_array[bank]);
    if (task_handle_array[bank] == NULL)
    {
        ESP_LOGE(TAG, "%s creation failed", taskName)
    }
    else
    {
        ESP_LOGI(TAG, "%s task creation successful", taskName);
    }
}

void ct_start_time_task(dm_BANK_SELECT bank)
{
    _ct_start_task(bank, &_ct_time_task, "time_task");
}

static void _ct_date_task(dm_BANK_SELECT bank)
{
    ESP_LOGI(TAG, "Waiting for wifi to connect");
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, SNTP_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    while (1)
    {
        char display[5] = "";
        tm_getLocalDateText("%2d.%d", display);

        if (bank == RIGHT)
        {
            dm_setRightBank(display);
        }
        else
        {
            dm_setLeftBank(display);
        }
        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void ct_start_date_task(dm_BANK_SELECT bank)
{
    _ct_start_task(bank, &_ct_date_task, "date_task");
}

void ct_stop_task(dm_BANK_SELECT bank)
{
    ESP_LOGI(TAG, "stopping task running on the %s bank", bank == LEFT ? "LEFT" : "RIGHT");

    if (task_handle_array[bank] != NULL)
    {
        vTaskDelete(task_handle_array[bank]);
        task_handle_array[bank] = NULL;
        dm_clear_bank(bank);
    }
}
