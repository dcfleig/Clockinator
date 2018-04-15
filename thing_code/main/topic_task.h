#ifndef TOPIC_TASK_H
#define TOPIC_TASK_H
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
#include "esp_task_wdt.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

#include "app_constants.h"
#include "display_manager.h"
#include "source_router.h"

void tt_start_topic_task();
void tt_stop_topic_task();

void tt_publish_message(const char *topic, const char *message);

void tt_bank_subscribe(dm_BANK_SELECT bank, const char *topic);

void tt_bank_unsubscribe(dm_BANK_SELECT bank);

void tt_setTopicCallback(const char *topic, void (*fptr)());

#endif