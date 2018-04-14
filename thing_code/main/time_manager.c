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

static const char *TAG = "time_manager";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern const int SHADOW_IN_PROGRESS_BIT;
extern const int MQTT_SEND_IN_PROGRESS_BIT;
extern const int SOURCE_ROUTER_STARTED_BIT;
extern EventGroupHandle_t app_event_group;

char _sntp_hostname[30] = "pool.ntp.org";
char _sntp_timezone[30] = "EST5EDT,M3.2.0/2,M11.1.0";

void tm_init()
{
    ESP_LOGI(TAG, "Starting SNTP");
    ESP_LOGI(TAG, "Setting TZ to %s", _sntp_timezone);

    setenv("TZ", _sntp_timezone, 1);
    tzset();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, _sntp_hostname);
    sntp_init();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    char strftime_buf[64];
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time on the device is: %s", strftime_buf);
    xEventGroupSetBits(app_event_group, SNTP_CONNECTED_BIT);
}

const char *tm_getNTPServer() { return _sntp_hostname; }

void tm_setNTPServer(const char *ntpServer)
{
    strcpy(_sntp_hostname, ntpServer);
    sntp_stop();
    tm_init();
}

const char *tm_getTimezone() { return _sntp_timezone; }

void tm_setTimezone(const char *timeZone)
{
    strcpy(_sntp_timezone, timeZone);
    sntp_stop();
    tm_init();
}

struct tm tm_getLocalTime()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

void tm_getLocalTimeText(const char *format, char *buffer)
{
    struct tm localTime = tm_getLocalTime();
    int hour = localTime.tm_hour;
    if (hour == 0)
        hour = 12;
    if (hour > 12)
        hour -= 12;
    sprintf(buffer, format, hour, localTime.tm_min);
}

void tm_getLocalDateText(const char *format, char *buffer)
{
    struct tm localTime = tm_getLocalTime();
    sprintf(buffer, format, localTime.tm_mon + 1, localTime.tm_mday);
}

char *tm_getLocalDateTimeText()
{
    struct tm now = tm_getLocalTime();
    char *currentTimeArray = asctime(&now);
    int currentArrayLength = strlen(currentTimeArray) - 1; // going to remove \n

    currentTimeArray[currentArrayLength] = '\0'; // Overwrite the \n with \0

    return currentTimeArray;
}
