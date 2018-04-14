#ifndef CLOCKS_TASK_H
#define CLOCKS_TASK_H
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
#include "esp_task_wdt.h"

#include "app_constants.h"
#include "display_manager.h"
#include "time_manager.h"

void ct_start_time_task(dm_BANK_SELECT bank);
void ct_start_date_task(dm_BANK_SELECT bank);

void ct_stop_task(dm_BANK_SELECT bank);

#endif