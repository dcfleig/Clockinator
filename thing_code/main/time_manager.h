#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "apps/sntp/sntp.h"

void tm_init();

char *tm_getNTPServer();
void tm_setNTPServer(const char *ntpServer);

char *tm_getTimezone();
void tm_setTimezone(const char *timeZone);

struct tm tm_getLocalTime();

void tm_getLocalTimeText(const char *format, char *buffer);
void tm_getLocalDateText(const char *format, char *buffer);
char *tm_getLocalDateTimeText();

#endif