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

#include "display_manager.h"
#include "time_manager.h"

void time_task(void *param);
void date_task(void *param);

#endif