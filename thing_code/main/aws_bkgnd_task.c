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
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 500

static const char *TAG = "aws_bkgnd_task";
static TaskHandle_t shadow_task_handle = NULL;
AWS_IoT_Client mqttClient;


void aws_start_task(void *) {

}

void _shadow_task(void *param)
{
    IoT_Error_t rc = FAILURE;

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    // initialize the mqtt client
    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = CONFIG_AWS_IOT_MQTT_HOST;
    sp.port = CONFIG_AWS_IOT_MQTT_PORT;
    sp.pClientCRT = (const char *)certificate_pem_crt_start;
    sp.pClientKey = (const char *)private_pem_key_start;
    sp.pRootCA = (const char *)aws_root_ca_pem_start;
    sp.enableAutoReconnect = false;
    sp.disconnectHandler = NULL;

    ESP_LOGI(TAG, "Shadow Init");
    rc = aws_iot_shadow_init(&mqttClient, &sp);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "aws_iot_shadow_init returned error %d, aborting...", rc);
        abort();
    }

    ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
    scp.pMyThingName = CONFIG_AWS_EXAMPLE_THING_NAME;
    scp.pMqttClientId = CONFIG_AWS_EXAMPLE_CLIENT_ID;
    scp.mqttClientIdLen = (uint16_t)strlen(CONFIG_AWS_EXAMPLE_CLIENT_ID);

    ESP_LOGD(TAG, "Shadow Connect");
    rc = aws_iot_shadow_connect(&mqttClient, &scp);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "aws_iot_shadow_connect returned error %d, aborting...", rc);
        abort();
    }

    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d, aborting...", rc);
        abort();
    }

    jsonStruct_t sntpHandler;
    sntpHandler.cb = sntp_hostname_Callback;
    sntpHandler.pKey = "sntpHostname";
    sntpHandler.pData = &shadow_ntpServer;
    sntpHandler.type = SHADOW_JSON_STRING;
    rc = aws_iot_shadow_register_delta(&mqttClient, &sntpHandler);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register sntpHandler Delta Error");
    }

    jsonStruct_t timezoneHandler;
    timezoneHandler.cb = timezone_Callback;
    timezoneHandler.pKey = "timezone";
    timezoneHandler.pData = &shadow_timezone;
    timezoneHandler.type = SHADOW_JSON_STRING;
    rc = aws_iot_shadow_register_delta(&mqttClient, &timezoneHandler);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register timezoneHandler Delta Error");
    }

    jsonStruct_t localtimeHandler;
    localtimeHandler.cb = NULL;
    localtimeHandler.pKey = "deviceTime";
    localtimeHandler.pData = &shadow_localtime;
    localtimeHandler.type = SHADOW_JSON_STRING;

    jsonStruct_t leftSourceSlotHandler;
    leftSourceSlotHandler.cb = leftSourceSlotHandler_Callback;
    leftSourceSlotHandler.pKey = "leftSource";
    leftSourceSlotHandler.pData = &shadow_leftBankSource;
    leftSourceSlotHandler.type = SHADOW_JSON_STRING;
    rc = aws_iot_shadow_register_delta(&mqttClient, &leftSourceSlotHandler);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register leftSourceSlotHandler Delta Error");
    }

    jsonStruct_t rightSourceSlotHandler;
    rightSourceSlotHandler.cb = rightSourceSlotHandler_Callback;
    rightSourceSlotHandler.pKey = "rightSource";
    rightSourceSlotHandler.pData = &shadow_rightBankSource;
    rightSourceSlotHandler.type = SHADOW_JSON_STRING;
    rc = aws_iot_shadow_register_delta(&mqttClient, &rightSourceSlotHandler);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register rightSourceSlotHandler Delta Error");
    }

    jsonStruct_t brightnessHandler;
    brightnessHandler.cb = brightnessHandler_Callback;
    brightnessHandler.pKey = "brightness";
    brightnessHandler.pData = &shadow_brightness;
    brightnessHandler.type = SHADOW_JSON_INT8;
    rc = aws_iot_shadow_register_delta(&mqttClient, &brightnessHandler);
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register brightnessHandler Delta Error");
    }

    jsonStruct_t batteryHandler;
    batteryHandler.cb = NULL;
    batteryHandler.pKey = "batteryVoltageMV";
    batteryHandler.pData = &shadow_battery_voltage;
    batteryHandler.type = SHADOW_JSON_UINT32;
    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Shadow Register brightnessHandler Delta Error");
    }

    //Subscribe this task to TWDT, then check if it is subscribed
    // CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    // CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);

    while (1)
    {
        // CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK); //Comment this line to trigger a TWDT timeout

        rc = aws_iot_shadow_yield(&mqttClient, 200);
        if (NETWORK_ATTEMPTING_RECONNECT == rc || shadowUpdateInProgress)
        {
            rc = aws_iot_shadow_yield(&mqttClient, 1000);
            // If the client is attempting to reconnect, or already waiting on a shadow update,
            // we will skip the rest of the loop.
            continue;
        }

        shadow_battery_voltage = getBatteryVoltage();
        strcpy(shadow_localtime, tm_getLocalDateTimeText());

        if (SUCCESS != rc)
        {
            ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
        }

        ESP_LOGD(TAG, "Building shadow update json");
        rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
        if (SUCCESS == rc)
        {
            rc = aws_iot_shadow_add_reported(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 7,
                                             &sntpHandler,
                                             &timezoneHandler,
                                             &leftSourceSlotHandler,
                                             &rightSourceSlotHandler,
                                             &brightnessHandler,
                                             &localtimeHandler,
                                             &batteryHandler);
            if (SUCCESS == rc)
            {
                rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
                if (SUCCESS == rc)
                {
                    ESP_LOGD(TAG, "Updating Shadow: %s", JsonDocumentBuffer);
                    rc = aws_iot_shadow_update(&mqttClient, CONFIG_AWS_EXAMPLE_THING_NAME, JsonDocumentBuffer,
                                               ShadowUpdateStatusCallback, NULL, 5, true);
                    shadowUpdateInProgress = true;
                }
            }
        }

        ESP_LOGD(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));

        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    ESP_LOGD(TAG, "Disconnecting");
    rc = aws_iot_shadow_disconnect(&mqttClient);

    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "Disconnect error %d", rc);
    }

    vTaskDelete(NULL);
}

