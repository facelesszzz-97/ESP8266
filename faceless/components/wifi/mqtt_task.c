#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "mqtt_client.h"
#include "cJSON.h"

#include "mqtt_task.h"
#include "wifi_task.h"

#include <stdlib.h>

#define MQTT_SUBSCRIBED_BIT    BIT0
#define MQTT_RECEIVE_DATA_BIT  BIT1

static const char *TAG = "mqtt_event_handler";
static const char *TAG_TASK = "mqtt_task";
static EventGroupHandle_t EventGroupHandle_Mqtt;
static QueueHandle_t QueueHandle_Mqtt;

static esp_mqtt_client_handle_t gMqtt_Task_Client;

// #define MQTT_TASK_STACK         4096
#define MQTT_TASK_PRI           4
TaskHandle_t TaskHandle_Mqtt;

#define MQTT_REC_TASK_PRI       5
TaskHandle_t TaskHandle_MqttReceive;

static const char *gMethod = "method";
static const char *gClientToken = "clientToken";
static const char *gParams = "params";
static const char *gLed = "Led";

static int gDataLen;

void Mqtt_Rec_Task(void *parameter)
{
    cJSON *lRecMqttData;
    cJSON *lMethod, *lClientToken, *lParams, *lLed;
    char *lRecData;
    int lBit;
    while(1)
    {
        lBit = xEventGroupWaitBits(EventGroupHandle_Mqtt, MQTT_RECEIVE_DATA_BIT, true, false, portMAX_DELAY);
        if(lBit & MQTT_RECEIVE_DATA_BIT){
            lRecData = (char *)pvPortMalloc(gDataLen);
            // sprintf(lRecData, "%.*s", gMqtt_Task_Event->data_len, gMqtt_Task_Event->data);
            if(QueueHandle_Mqtt != NULL && xQueueReceive(QueueHandle_Mqtt, lRecData, 0)){
                ESP_LOGI(TAG_TASK, "\r\n%s", lRecData);
                // vQueueDelete(QueueHandle_Mqtt);
            }
            // xEventGroupClearBits(EventGroupHandle_Mqtt, MQTT_RECEIVE_DATA_BIT);
            lRecMqttData = cJSON_Parse(lRecData);
            if(lRecMqttData != NULL){
                lMethod = cJSON_GetObjectItem(lRecMqttData, gMethod);
                if(cJSON_IsString(lMethod)){
                    ESP_LOGI(TAG_TASK, "%s", lMethod->valuestring);
                }
                lClientToken = cJSON_GetObjectItem(lRecMqttData, gClientToken);
                if(cJSON_IsString(lClientToken)){
                    ESP_LOGI(TAG_TASK, "%s", lClientToken->valuestring);
                }
                lParams = cJSON_GetObjectItem(lRecMqttData, gParams);
                // if(cJSON_IsString(lParams)){
                //     ESP_LOGI(TAG_TASK, "\r\n%s", lParams->valuestring);
                // }
                if(lParams != NULL){
                    lLed = cJSON_GetObjectItem(lParams, gLed);
                    if(cJSON_IsNumber(lLed)){
                        ESP_LOGI(TAG_TASK, "%d", lLed->valueint);
                    }
                }
                
            }
            vPortFree(lRecData);
            lRecData = NULL;
            cJSON_Delete(lRecMqttData);
        }
    }
}

void Mqtt_Task(void *parameter)
{
    cJSON *lRoot, *lNext;
    int msg_id;
    int lBit;
    uint8_t lLedBit = 0;
    while(1)
    {
        lBit = xEventGroupGetBits(EventGroupHandle_Mqtt);
        if(lBit & MQTT_SUBSCRIBED_BIT){
            lLedBit = !lLedBit;
            lRoot = cJSON_CreateObject();
            lNext = cJSON_CreateObject();
            cJSON_AddItemToObject(lRoot, gMethod, cJSON_CreateString("report"));
            cJSON_AddItemToObject(lRoot, gClientToken, cJSON_CreateString("lmc.li"));
            cJSON_AddItemToObject(lRoot, gParams, lNext);
            cJSON_AddItemToObject(lNext, gLed, cJSON_CreateNumber(lLedBit));
            /* Don't use cJSON_Print() because the string has '\t'*/
            ESP_LOGI(TAG_TASK, "\r\n%s", cJSON_PrintUnformatted(lRoot));
            msg_id = esp_mqtt_client_publish(gMqtt_Task_Client, "$thing/up/property/VPJ2KX93R4/control", cJSON_PrintUnformatted(lRoot), 0, 0, 0);
            ESP_LOGI(TAG_TASK, "sent publish successful, msg_id=%d", msg_id);
            cJSON_Delete(lRoot);
        }
        vTaskDelay(5000/portTICK_PERIOD_MS);
    }
} 

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    gMqtt_Task_Client = client;
    int msg_id;
    char *lSendData;
    // static bool lMqtt_state = false;
    ESP_LOGI(TAG, "%d", event->event_id);
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // msg_id = esp_mqtt_client_publish(client, "$thing/up/property/VPJ2KX93R4/control", cJSON_Print(lRoot), 0, 0, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "$thing/down/property/VPJ2KX93R4/control", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            // lMqtt_state = true;
            // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            // msg_id = esp_mqtt_client_unsubscribe(client, "$thing/down/property/VPJ2KX93R4/control");
            // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            // lMqtt_state = false;
            xEventGroupClearBits(EventGroupHandle_Mqtt, 0xff);
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            xEventGroupSetBits(EventGroupHandle_Mqtt, MQTT_SUBSCRIBED_BIT);

            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            // msg_id = esp_mqtt_client_publish(client, "$thing/up/property/VPJ2KX93R4/control", cJSON_Print(lRoot), 0, 0, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            xEventGroupClearBits(EventGroupHandle_Mqtt, 0xff);
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            // printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            // printf("DATA=%.*s\r\n", event->data_len, event->data);
            lSendData = (char *)pvPortMalloc(event->data_len);
            QueueHandle_Mqtt = xQueueCreate(1, event->data_len);
            sprintf(lSendData, "%.*s\r\n", event->data_len, event->data);
            // memcpy(lSendData, event->data, event->data_len);
            gDataLen = event->data_len;
            xEventGroupSetBits(EventGroupHandle_Mqtt, MQTT_RECEIVE_DATA_BIT);
            if(QueueHandle_Mqtt != NULL)
                xQueueSend(QueueHandle_Mqtt, lSendData, 0);
            // msg_id = esp_mqtt_client_publish(client, "$thing/up/property/VPJ2KX93R4/control", cJSON_Print(lRoot), 0, 0, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            vPortFree(lSendData);
            lSendData = NULL;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            // if(true == lMqtt_state){
            //     esp_mqtt_client_stop(client);
            //     Wifi_Task_Init();
            // }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    // cJSON_Delete(lRoot);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

void Mqtt_Task_Init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .port = 1883,
        .username = "VPJ2KX93R4control;12010126;B3BTS;1638860525",
        .password = "a19fb06de3567f007eb563bddbc10814eb5a7d3b8f6c1518bdf205f53de8cfe9;hmacsha256",
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    EventGroupHandle_Mqtt = xEventGroupCreate();
    xTaskCreate((TaskFunction_t) Mqtt_Task,
                (const char *) "Mqtt_Task",		    /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                (configSTACK_DEPTH_TYPE) MQTT_TASK_STACK,
                (void * ) NULL,
                (UBaseType_t) MQTT_TASK_PRI,
                (TaskHandle_t *) &TaskHandle_Mqtt);
    xTaskCreate((TaskFunction_t) Mqtt_Rec_Task,
                (const char *) "Mqtt_Rec_Task",		/*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                (configSTACK_DEPTH_TYPE) MQTT_TASK_STACK,
                (void * ) NULL,
                (UBaseType_t) MQTT_REC_TASK_PRI,
                (TaskHandle_t *) &TaskHandle_MqttReceive);
}