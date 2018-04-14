#ifndef SOURCE_ROUTER_H
#define SOURCE_ROUTER_H
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

#include "app_constants.h"
#include "display_manager.h"
#include "clock_tasks.h"

typedef struct SourceRouterMessage
{
    dm_BANK_SELECT bank;
    const char *dataSource;
} _SourceRouterMessage;

QueueHandle_t source_router_queue;

void start_source_router_task(void *param);
void _source_router_task(void *param);
void stop_source_router_task();

#endif