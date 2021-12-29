#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_tls.h"
#include "bsp_usart.h"

#define EX_UART_NUM     UART_NUM_0

#define UART0_RX_BUFFER_SIZE  (1024)
#define UART0_TX_BUFFER_SIZE  (1024)
#define UART0_QUEUE_SIZE      (100)

#define UART0_EVENT_TASK_STACK     (1024*2)
#define UART0_EVENT_TASK_PRI       12
TaskHandle_t TaskHandle_Uart0;
QueueHandle_t QueueHandle_Uart0;

static const char *TAG = "Uart_Event_Task";

void Uart_Event_Task(void *parameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *) pvPortMalloc(UART0_RX_BUFFER_SIZE);

    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(QueueHandle_Uart0, (void *)&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, UART0_RX_BUFFER_SIZE);
            ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);

            switch (event.type) {
                // Event of UART receving data
                // We'd better handler data event fast, there would be much more data events than
                // other types of events. If we take too much time on data event, the queue might be full.
                case UART_DATA:
                    ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    ESP_LOGI(TAG, "[DATA EVT]:%.*s", event.size, (const char *) dtmp);
                    // uart_write_bytes(EX_UART_NUM, (const char *) dtmp, event.size);
                    break;

                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(QueueHandle_Uart0);
                    break;

                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(QueueHandle_Uart0);
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;

                // Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;

                // Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }

    vPortFree(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void Bsp_Usart_Init(void)
{
    uart_config_t lUartConfig = {
        .baud_rate = 74800,                         /*!< UART baud rate*/
        .data_bits = UART_DATA_8_BITS,              /*!< UART byte size*/
        .parity = UART_PARITY_DISABLE,              /*!< UART parity mode*/
        .stop_bits = UART_STOP_BITS_1,              /*!< UART stop bits*/
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,      /*!< UART HW flow control mode (cts/rts)*/
    };
    
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &lUartConfig));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART0_RX_BUFFER_SIZE * 2, UART0_RX_BUFFER_SIZE * 2, UART0_QUEUE_SIZE, &QueueHandle_Uart0, 0));

    // Create a task to handler UART event from ISR
    // xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

    xTaskCreate((TaskFunction_t) Uart_Event_Task,
                (const char *) "Uart_Event_Task",		/*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                (configSTACK_DEPTH_TYPE) UART0_EVENT_TASK_STACK,
                (void * ) NULL,
                (UBaseType_t) UART0_EVENT_TASK_PRI,
                (TaskHandle_t *) &TaskHandle_Uart0);
}