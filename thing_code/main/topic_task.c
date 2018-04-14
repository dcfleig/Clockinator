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

#include "display_manager.h"
#include "source_router.h"
#include "topic_task.h"

static const char *TAG = "topic_task";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern const int SHADOW_IN_PROGRESS_BIT;
extern const int MQTT_SEND_IN_PROGRESS_BIT;
extern const int SOURCE_ROUTER_STARTED_BIT;
extern EventGroupHandle_t app_event_group;
extern AWS_IoT_Client mqttClient;
extern SemaphoreHandle_t mqttMutex;

static TaskHandle_t task_handle;

void _iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                     IoT_Publish_Message_Params *params, void *pData)
{
    ESP_LOGI(TAG, "Subscribe callback");
    ESP_LOGI(TAG, "%.*s\t%.*s : Bank %s", topicNameLen, topicName, (int)params->payloadLen, (char *)params->payload, (dm_BANK_SELECT *)pData == LEFT ? "LEFT" : "RIGHT");

    char tmp[200] = "";
    strncpy(tmp, (char *)params->payload, (int)params->payloadLen);
    memset(tmp, '\0', strlen(tmp));

    dm_setBankChars(*(dm_BANK_SELECT *)pData, tmp);
}

void tt_setTopicCallback(const char *topic, void (*fptr)())
{
    _iot_subscribe_callback_handler(&mqttClient, topic, strlen(topic), &fptr, NULL);
}

void _tt_topic_task(void *params)
{
    IoT_Error_t rc = FAILURE;

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    // while ((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc))
    while (1)
    {
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK); //Comment this line to trigger a TWDT timeout
        // Don't need to yield here as it supposedly happens as part of the shadow yield

        // rc = aws_iot_mqtt_yield(&mqttClient, 200);
        // while (NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_DISCONNECTED_ERROR == rc || MQTT_CLIENT_NOT_IDLE_ERROR == rc)
        // {
        //     rc = aws_iot_mqtt_yield(&mqttClient, 200);
        //     // If the client is attempting to reconnect, or already waiting on a shadow update,
        //     // we will skip the rest of the loop.
        // }

        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(1000 / portTICK_RATE_MS);
        //tt_publish_message("test_topic/esp32", "snorgie borgies");
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
}

void tt_bank_subscribe(dm_BANK_SELECT bank, const char *topic)
{
    IoT_Publish_Message_Params paramsQOS0;
    paramsQOS0.qos = QOS0;
    paramsQOS0.isRetained = 0;

    IoT_Error_t rc = FAILURE;

    if (strcasecmp(dm_getBankSource(bank), topic) == 0)
    {
        ESP_LOGI(TAG, "Already subscribed to topic %s...", dm_getBankSource(bank));
        return;
    }
    strcpy(dm_getBankSource(bank), topic);

    const int TOPIC_LEN = strlen(dm_getBankSource(bank));

    ESP_LOGI(TAG, "Subscribing to %s for bank %s", dm_getBankSource(bank), bank == LEFT ? "LEFT" : "RIGHT");
    rc = aws_iot_mqtt_subscribe(&mqttClient, dm_getBankSource(bank), TOPIC_LEN, QOS0, _iot_subscribe_callback_handler, (dm_BANK_SELECT *)bank);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Error subscribing : %d ", rc);
        return;
    }
}

void tt_start_topic_task()
{
    if (task_handle != NULL)
    {
        ESP_LOGI(TAG, "topic_task already running");
        return;
    }

    ESP_LOGI(TAG, "starting the topic_task");
    xTaskCreate(&_tt_topic_task, "_tt_topic_task", TOPIC_TASK_STACK_DEPTH, NULL, TOPIC_TASK_PRIORITY, &task_handle);
    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "topic_task creation failed")
    }
    else
    {
        ESP_LOGI(TAG, "topic_task task creation successful");
    }
}

void _tt_unsubscribe(dm_BANK_SELECT bank)
{
    IoT_Error_t rc = FAILURE;
    ESP_LOGI(TAG, "Unsubscribing from topic %s on bank %s", dm_getBankSource(bank), bank == LEFT ? "LEFT" : "RIGHT");
    rc = aws_iot_mqtt_unsubscribe(&mqttClient, dm_getBankSource(bank), strlen(dm_getBankSource(bank)));
    if (SUCCESS == rc)
    {
        dm_setBankSource(bank, "");
    }
}

void tt_stop_topic_task()
{
    ESP_LOGI(TAG, "stopping topic_task ");

    if (task_handle != NULL)
    {
        // Try unsubscribing from all the topics first
        _tt_unsubscribe(LEFT);
        _tt_unsubscribe(RIGHT);

        vTaskDelete(task_handle);
        task_handle = NULL;
    }
}

void tt_publish_message(const char *topic, const char *message)
{
    IoT_Error_t rc = FAILURE;

    const int TOPIC_LEN = strlen(topic);
    const int MESSAGE_LEN = strlen(message);

    ESP_LOGI(TAG, "Sending %s to topic %s", message, topic);

    IoT_Publish_Message_Params paramsQOS0;
    paramsQOS0.qos = QOS0;
    paramsQOS0.isRetained = 0;
    paramsQOS0.payloadLen = MESSAGE_LEN;
    paramsQOS0.payload = (void *)message;

    ESP_LOGD(TAG, "MQTT client state: %d", (int)mqttClient.clientStatus.clientState);
    ESP_LOGD(TAG, "MQTT payload: %s", message);
    ESP_LOGD(TAG, "MQTT topic: %s", topic);
    ESP_LOGD(TAG, "MQTT topic length: %d", TOPIC_LEN);

    rc = aws_iot_mqtt_yield(&mqttClient, 200);
    while (NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_DISCONNECTED_ERROR == rc || MQTT_CLIENT_NOT_IDLE_ERROR == rc)
    {
        rc = aws_iot_mqtt_yield(&mqttClient, 200);
        // If the client is attempting to reconnect, or already waiting on a shadow update,
        // we will skip the rest of the loop.
    }

    rc = aws_iot_mqtt_publish(&mqttClient, topic, TOPIC_LEN, &paramsQOS0);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "An error occurred sending the message to the topic %d", rc);
    }
}
