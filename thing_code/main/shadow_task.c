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

#include "nvs.h"
#include "nvs_flash.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#include "display_manager.h"
#include "clock_tasks.h"
#include "shadow_task.h"
#include "source_router.h"

static const char *TAG = "shadow_task";

extern const int IP_CONNECTED_BIT;
extern const int SNTP_CONNECTED_BIT;
extern const int SHADOW_CONNECTED_BIT;
extern EventGroupHandle_t app_event_group;
extern QueueHandle_t source_router_queue;

static TaskHandle_t shadow_task_handle = NULL;

AWS_IoT_Client mqttClient;

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

static bool shadowUpdateInProgress;

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
        ESP_LOGI(TAG, "Update accepted");
        xEventGroupSetBits(app_event_group, SHADOW_CONNECTED_BIT);
    }
}

void sntp_hostname_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        tm_setNTPServer((char *)(pContext->pData));
    }
}

void timezone_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        tm_setTimezone((char *)(pContext->pData));
    }
}

void leftSourceSlotHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        ESP_LOGI(TAG, "Creating SourceRouterMessage to send to source_route_task");
        struct SourceRouterMessage srm;
        srm.bank = LEFT;
        srm.data = (char *)(pContext->pData);
        ESP_LOGI(TAG, "SourceRouterMessage bank: %s", srm.bank == LEFT
                                                          ? "LEFT"
                                                          : "RIGHT");
        ESP_LOGI(TAG, "SourceRouterMessage data: %s", srm.data);

        xQueueSend(source_router_queue, &srm, (TickType_t)0);
    }
}

void rightSourceSlotHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - %s value changed to %s", (char *)(pContext->pKey), (char *)(pContext->pData));
        ESP_LOGI(TAG, "Creating SourceRouterMessage to send to source_route_task");
        struct SourceRouterMessage srm;
        srm.bank = RIGHT;
        srm.data = (char *)(pContext->pData);
        ESP_LOGI(TAG, "SourceRouterMessage bank: %s", srm.bank == LEFT
                                                          ? "LEFT"
                                                          : "RIGHT");
        ESP_LOGI(TAG, "SourceRouterMessage data: %s", srm.data);

        xQueueSend(source_router_queue, &srm, (TickType_t)0);
    }
}

void brightnessHandler_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext)
{
    IOT_UNUSED(pJsonString);
    IOT_UNUSED(JsonStringDataLen);
    if (pContext != NULL)
    {
        ESP_LOGI(TAG, "Delta - %s value changed to %d", (char *)(pContext->pKey), (int)(pContext->pData));
        int brightness = (int)pContext->pData;
        dm_setBrightness(brightness);
    }
}

void shadow_task(void *param)
{
    IoT_Error_t rc = FAILURE;
    char shadow_ntpServer[15] = "";
    char shadow_timezone[30] = "";
    char shadow_localtime[30] = "";
    char shadow_leftBankSource[20] = "";
    char shadow_rightBankSource[20] = "";
    int shadow_brightness = dm_getBrightness();

    strcpy(shadow_ntpServer, tm_getNTPServer());
    strcpy(shadow_timezone, tm_getTimezone());
    strcpy(shadow_leftBankSource, dm_getLeftBank());
    strcpy(shadow_rightBankSource, dm_getRightBank());
    strcpy(shadow_localtime, tm_getLocalDateTimeText());

    char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
    size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    // initialize the mqtt client
    AWS_IoT_Client mqttClient;

    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = AWS_IOT_MQTT_HOST;
    sp.port = AWS_IOT_MQTT_PORT;

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

    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(app_event_group, IP_CONNECTED_BIT,
                        false, true, portMAX_DELAY);

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

    ESP_LOGI(TAG, "Shadow Connect");
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
    localtimeHandler.pKey = "localTime";
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

    // ************** Shadow delta here **************************

    // loop and publish a change in temperature
    while (NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)
    {
        strcpy(shadow_localtime, tm_getLocalDateTimeText());

        rc = aws_iot_shadow_yield(&mqttClient, 200);
        if (NETWORK_ATTEMPTING_RECONNECT == rc || shadowUpdateInProgress)
        {
            rc = aws_iot_shadow_yield(&mqttClient, 1000);
            // If the client is attempting to reconnect, or already waiting on a shadow update,
            // we will skip the rest of the loop.
            continue;
        }

        rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
        if (SUCCESS == rc)
        {
            rc = aws_iot_shadow_add_reported(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 6,
                                             &sntpHandler,
                                             &timezoneHandler,
                                             &localtimeHandler,
                                             &leftSourceSlotHandler,
                                             &rightSourceSlotHandler,
                                             &brightnessHandler);
            if (SUCCESS == rc)
            {
                rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
                if (SUCCESS == rc)
                {
                    ESP_LOGI(TAG, "Update Shadow: %s", JsonDocumentBuffer);
                    rc = aws_iot_shadow_update(&mqttClient, CONFIG_AWS_EXAMPLE_THING_NAME, JsonDocumentBuffer,
                                               ShadowUpdateStatusCallback, NULL, 20, true);
                    shadowUpdateInProgress = true;
                }
            }
        }
        ESP_LOGI(TAG, "*** Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));

        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    if (SUCCESS != rc)
    {
        ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
    }

    ESP_LOGI(TAG, "Disconnecting");
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
        ESP_LOGI(TAG, "Starting _shadow_task...");
        xTaskCreate(&shadow_task, "_shadow_task", SHADOW_TASK_STACK_DEPTH, param, SHADOW_TASK_PRIORITY, &shadow_task_handle);
        if (shadow_task_handle == NULL)
        {
            ESP_LOGE(TAG, "_shadow_task creation failed");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Was going to start _shadow_task but it's already running");
    }
}

void stop_shadow_task()
{
    ESP_LOGI(TAG, "Stopping _shadow_task...");
    vTaskDelete(shadow_task_handle);
    while (shadow_task_handle != NULL)
    {
        vTaskDelay(100 / portTICK_RATE_MS);
    }
    ESP_LOGI(TAG, "_shadow_task stopped");
}