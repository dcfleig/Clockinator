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
#include "driver/adc.h"
#include "esp_adc_cal.h"
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
#include "shadow_task.h"
#include "source_router.h"

#define BATTERY_ADC_PIN A13 // I35 , A1_7

static const char *TAG = "shadow_task";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern const int SHADOW_IN_PROGRESS_BIT;
extern const int MQTT_SEND_IN_PROGRESS_BIT;
extern const int SOURCE_ROUTER_STARTED_BIT;

extern EventGroupHandle_t app_event_group;
extern QueueHandle_t source_router_queue;
extern SemaphoreHandle_t mqttMutex;

static TaskHandle_t shadow_task_handle = NULL;

#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 500

/* CA Root certificate, device ("Thing") certificate and device
 * ("Thing") key.

   Example can be configured one of two ways:

   "Embedded Certs" are loaded from files in "certs/" and embedded into the app binary.

   "Filesystem Certs" are loaded from the filesystem (SD card, etc.)

   See example README for more details.
*/
#if defined(CONFIG_EXAMPLE_EMBEDDED_CERTS)

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");

#elif defined(CONFIG_EXAMPLE_FILESYSTEM_CERTS)

static const char *DEVICE_CERTIFICATE_PATH = CONFIG_EXAMPLE_CERTIFICATE_PATH;
static const char *DEVICE_PRIVATE_KEY_PATH = CONFIG_EXAMPLE_PRIVATE_KEY_PATH;
static const char *ROOT_CA_PATH = CONFIG_EXAMPLE_ROOT_CA_PATH;

#else
#error "Invalid method for loading certs"
#endif

void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
                                const char *pReceivedJsonDocument, void *pContextData)
{
    IOT_UNUSED(pThingName);
    IOT_UNUSED(action);
    IOT_UNUSED(pReceivedJsonDocument);
    IOT_UNUSED(pContextData);

    shadowUpdateInProgress = false;

    if (SHADOW_ACK_TIMEOUT == status)
    {
        ESP_LOGE(TAG, "Update timed out");
    }
    else if (SHADOW_ACK_REJECTED == status)
    {
        ESP_LOGE(TAG, "Update rejected");
    }
    else if (SHADOW_ACK_ACCEPTED == status)
    {
        ESP_LOGD(TAG, "Update accepted");
        xEventGroupSetBits(app_event_group, SHADOW_CONNECTED_BIT);
    }
}

void sntp_hostname_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGD(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        tm_setNTPServer((char *)(pContext->pData));
    }
}

void timezone_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGD(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        tm_setTimezone((char *)(pContext->pData));
    }
}

void leftSourceSlotHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGD(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        ESP_LOGI(TAG, "Creating SourceRouterMessage to send to source_route_task");
        struct SourceRouterMessage srm;
        srm.bank = LEFT;
        srm.dataSource = (char *)(pContext->pData);
        ESP_LOGI(TAG, "SourceRouterMessage bank: %s", srm.bank == LEFT
                                                          ? "LEFT"
                                                          : "RIGHT");
        ESP_LOGI(TAG, "SourceRouterMessage data: %s", srm.dataSource);

        xEventGroupWaitBits(app_event_group, SOURCE_ROUTER_STARTED_BIT, false, true, portMAX_DELAY);
        xQueueSend(source_router_queue, &srm, (TickType_t)0);
    }
}

void rightSourceSlotHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGD(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        ESP_LOGI(TAG, "Creating SourceRouterMessage to send to source_route_task");
        struct SourceRouterMessage srm;
        srm.bank = RIGHT;
        srm.dataSource = (char *)(pContext->pData);
        ESP_LOGI(TAG, "SourceRouterMessage bank: %s", srm.bank == LEFT
                                                          ? "LEFT"
                                                          : "RIGHT");
        ESP_LOGI(TAG, "SourceRouterMessage data: %s", srm.dataSource);

        xEventGroupWaitBits(app_event_group, SOURCE_ROUTER_STARTED_BIT, false, true, portMAX_DELAY);
        xQueueSend(source_router_queue, &srm, (TickType_t)0);
    }
}

void brightnessHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGD(TAG, "Delta - %s value changed to %d", (char *)(pContext->pKey), *(int8_t *)(pContext->pData));
        //int brightness = pContext->pData;
        dm_setBrightness(*(int8_t *)(pContext->pData));
    }
}

uint32_t getBatteryVoltage()
{
    static esp_adc_cal_characteristics_t *adc_chars;
    static const adc_channel_t channel = ADC_CHANNEL_7; //GPIO34 if ADC1, GPIO14 if ADC2
    static const adc_atten_t atten = ADC_ATTEN_DB_0;
    static const adc_unit_t unit = ADC_UNIT_1;

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, atten);

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, 1100, adc_chars);

    uint32_t adc_reading = 0;
    for (int i = 0; i < 64; i++)
    {
        adc_reading += adc1_get_raw((adc1_channel_t)channel);
    }

    adc_reading /= 64;
    //Convert adc_reading to voltage in mV
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
    //ESP_LOGI(TAG, "Raw: %d\tVoltage: %dmV\n", adc_reading, voltage);
    return voltage * 2;
}

void _shadow_task(void *param)
{
    IoT_Error_t rc = FAILURE;

    char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
    size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);

    char shadow_ntpServer[15] = "";
    char shadow_timezone[30] = "";
    char shadow_localtime[30] = "";
    char shadow_leftBankSource[20] = "";
    char shadow_rightBankSource[20] = "";
    int8_t shadow_brightness = 0;
    uint32_t shadow_battery_voltage = 0;

    strcpy(shadow_ntpServer, tm_getNTPServer());
    strcpy(shadow_timezone, tm_getTimezone());
    // strcpy(shadow_leftBankSource, dm_getBankSource(LEFT));
    // strcpy(shadow_rightBankSource, dm_getBankSource(RIGHT));
    strcpy(shadow_localtime, tm_getLocalDateTimeText());
    shadow_brightness = dm_getBrightness();

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    // initialize the mqtt client
    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = CONFIG_AWS_IOT_MQTT_HOST;
    sp.port = CONFIG_AWS_IOT_MQTT_PORT;

#if defined(CONFIG_EXAMPLE_EMBEDDED_CERTS)
    sp.pClientCRT = (const char *)certificate_pem_crt_start;
    sp.pClientKey = (const char *)private_pem_key_start;
    sp.pRootCA = (const char *)aws_root_ca_pem_start;
#elif defined(CONFIG_EXAMPLE_FILESYSTEM_CERTS)
    sp.pClientCRT = DEVICE_CERTIFICATE_PATH;
    sp.pClientKey = DEVICE_PRIVATE_KEY_PATH;
    sp.pRootCA = ROOT_CA_PATH;
#endif
    sp.enableAutoReconnect = false;
    sp.disconnectHandler = NULL;

#ifdef CONFIG_EXAMPLE_SDCARD_CERTS
    ESP_LOGI(TAG, "Mounting SD card...");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
    };
    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
        abort();
    }
#endif
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

void start_shadow_task(void *param)
{
    if (shadow_task_handle == NULL)
    {
        ESP_LOGD(TAG, "Starting _shadow_task...");
        xTaskCreate(&_shadow_task, "_shadow_task", SHADOW_TASK_STACK_DEPTH, param, SHADOW_TASK_PRIORITY, &shadow_task_handle);
        if (shadow_task_handle == NULL)
        {
            ESP_LOGE(TAG, "_shadow_task creation failed");
        }
    }
    else
    {
        ESP_LOGD(TAG, "Was going to start _shadow_task but it's already running");
    }
}

void stop_shadow_task()
{
    IoT_Error_t rc = FAILURE;
    ESP_LOGD(TAG, "Stopping _shadow_task...");

    // esp_task_wdt_delete(shadow_task_handle);

    ESP_LOGI(TAG, "Disconnecting");
    rc = aws_iot_shadow_disconnect(&mqttClient);

    vTaskDelete(shadow_task_handle);
    while (shadow_task_handle != NULL)
    {
        vTaskDelay(100 / portTICK_RATE_MS);
    }
    vTaskDelete(shadow_task_handle);
    ESP_LOGD(TAG, "_shadow_task stopped");
}