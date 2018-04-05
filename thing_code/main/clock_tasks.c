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

static const char *TAG = "clock_tasks";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern EventGroupHandle_t app_event_group;

void time_task(void *param)
{
    enum dm_BANK_SELECT bankSelected = (enum dm_BANK_SELECT)param;

    ESP_LOGI(TAG, "Waiting for wifi to connect");
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, SNTP_CONNECTED_BIT | SHADOW_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    while (1)
    {
        char display[5] = "";
        tm_getLocalTimeText("%2d.%0d", display);

        if (bankSelected == RIGHT)
        {
            dm_setRightBank(display);
        }
        else
        {
            dm_setLeftBank(display);
        }

        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void date_task(void *param)
{
    enum dm_BANK_SELECT bankSelected = (enum dm_BANK_SELECT)param;

    ESP_LOGI(TAG, "Waiting for wifi to connect");
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, SNTP_CONNECTED_BIT | SHADOW_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    while (1)
    {
        char display[5] = "";
        tm_getLocalDateText("%2d.%d", display);

        if (bankSelected == RIGHT)
        {
            dm_setRightBank(display);
        }
        else
        {
            dm_setLeftBank(display);
        }

        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}