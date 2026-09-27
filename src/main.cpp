#include <string.h>
#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "dht22.h"


/* ============================================================
   Hardware handles
   ============================================================ */

static UART_HandleTypeDef huart1;
static ADC_HandleTypeDef hadc1;


/* ============================================================
   PART 5 - SensorData structure
   ============================================================ */

struct SensorData
{
    float temperature;
    float humidity;
    int lightLevel;
    bool motionDetected;
};


/* ============================================================
   PART 5 - Sensor queue
   ============================================================ */

static QueueHandle_t sensorQueue = NULL;


/* ============================================================
   Error handler
   ============================================================ */

extern "C" void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}


/* ============================================================
   LED - PC13
   ============================================================ */

static void LED_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Blue Pill LED is active-low */
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_13,
        GPIO_PIN_SET
    );
}


/* ============================================================
   USART1
   PA9  = TX
   PA10 = RX
   ============================================================ */

static void UART1_Init(void)
{
    huart1.Instance = USART1;

    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}


/* ============================================================
   Serial logging
   ============================================================ */

static void Log(const char *msg)
{
    HAL_UART_Transmit(
        &huart1,
        (uint8_t *)msg,
        (uint16_t)strlen(msg),
        HAL_MAX_DELAY
    );
}


/* ============================================================
   Raw UART functions for fault reporting
   ============================================================ */

static void RawPutc(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0)
    {
    }

    USART1->DR = (uint8_t)c;
}


static void RawPuts(const char *s)
{
    while (*s)
    {
        RawPutc(*s++);
    }
}


static void RawPutNum(uint32_t n)
{
    char num[12];

    int i = 10;

    num[11] = '\0';

    do
    {
        num[i--] = (char)('0' + (n % 10));
        n /= 10;

    } while (n > 0 && i >= 0);

    RawPuts(&num[i + 1]);
}


/* ============================================================
   FreeRTOS assert handler
   ============================================================ */

extern "C" void vAssertCalled(
    const char *file,
    int line
)
{
    __disable_irq();

    GPIOC->BSRR =
        (uint32_t)GPIO_PIN_13 << 16;

    RawPuts("\r\nASSERT FAILED: ");
    RawPuts(file);
    RawPuts(" line ");
    RawPutNum((uint32_t)line);
    RawPuts("\r\n");

    for (;;)
    {
    }
}


/* ============================================================
   FreeRTOS stack overflow handler
   ============================================================ */

extern "C" void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName
)
{
    (void)xTask;

    __disable_irq();

    RawPuts(
        "\r\nSTACK OVERFLOW in task: "
    );

    RawPuts(pcTaskName);
    RawPuts("\r\n");

    for (;;)
    {
    }
}


/* ============================================================
   HAL timebase support

   TIM4 is HAL's tick before scheduler startup.
   ============================================================ */

extern "C" void HAL_TIM_PeriodElapsedCallback(
    TIM_HandleTypeDef *htim
)
{
    if (
        htim->Instance == TIM4 &&
        xTaskGetSchedulerState() ==
            taskSCHEDULER_NOT_STARTED
    )
    {
        HAL_IncTick();
    }
}


/* ============================================================
   Wokwi-compatible FreeRTOS port support
   ============================================================ */

extern "C" BaseType_t xPortConsumeTickYield(void);


/* ============================================================
   FreeRTOS idle hook
   ============================================================ */

extern "C" void vApplicationIdleHook(void)
{
    __WFI();

    if (xPortConsumeTickYield() != pdFALSE)
    {
        taskYIELD();
    }
}


/* ============================================================
   Task settings
   ============================================================ */

#define TASK_A_PERIOD_MS          1000
#define TASK_B_PERIOD_MS          1000

#define TASK_A_PRIORITY           1
#define TASK_B_PRIORITY           2

#define TASK_STACK_WORDS          256


#define SENSOR_PERIOD_MS          2000
#define SENSOR_TASK_PRIORITY      3
#define SENSOR_STACK_WORDS        384


#define QUEUE_TASK_PRIORITY       2
#define QUEUE_TASK_STACK_WORDS    384

#define SENSOR_QUEUE_LENGTH       5


/* ============================================================
   TASK A
   ============================================================ */

static void TaskA(void *argument)
{
    (void)argument;

    for (;;)
    {
        HAL_GPIO_TogglePin(
            GPIOC,
            GPIO_PIN_13
        );

        Log("Task A running\r\n");

        vTaskDelay(
            pdMS_TO_TICKS(
                TASK_A_PERIOD_MS
            )
        );
    }
}


/* ============================================================
   TASK B
   ============================================================ */

static void TaskB(void *argument)
{
    (void)argument;

    for (;;)
    {
        Log("Task B running\r\n");

        vTaskDelay(
            pdMS_TO_TICKS(
                TASK_B_PERIOD_MS
            )
        );
    }
}


/* ============================================================
   ADC1 initialization

   PA0 = ADC1 Channel 0
   Used by LDR
   ============================================================ */

static void ADC1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin =
        GPIO_PIN_0;

    GPIO_InitStruct.Mode =
        GPIO_MODE_ANALOG;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );


    hadc1.Instance =
        ADC1;

    hadc1.Init.ScanConvMode =
        ADC_SCAN_DISABLE;

    hadc1.Init.ContinuousConvMode =
        DISABLE;

    hadc1.Init.DiscontinuousConvMode =
        DISABLE;

    hadc1.Init.ExternalTrigConv =
        ADC_SOFTWARE_START;

    hadc1.Init.DataAlign =
        ADC_DATAALIGN_RIGHT;

    hadc1.Init.NbrOfConversion =
        1;


    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Log(
            "ERROR: ADC initialization failed\r\n"
        );

        while (1)
        {
        }
    }


    ADC_ChannelConfTypeDef channel = {0};

    channel.Channel =
        ADC_CHANNEL_0;

    channel.Rank =
        ADC_REGULAR_RANK_1;

    channel.SamplingTime =
        ADC_SAMPLETIME_71CYCLES_5;


    if (
        HAL_ADC_ConfigChannel(
            &hadc1,
            &channel
        ) != HAL_OK
    )
    {
        Log(
            "ERROR: ADC channel configuration failed\r\n"
        );

        while (1)
        {
        }
    }


    if (
        HAL_ADCEx_Calibration_Start(
            &hadc1
        ) != HAL_OK
    )
    {
        Log(
            "ERROR: ADC calibration failed\r\n"
        );

        while (1)
        {
        }
    }
}


/* ============================================================
   Read raw LDR ADC value
   ============================================================ */

static uint16_t LDR_ReadRaw(void)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0;
    }

    if (
        HAL_ADC_PollForConversion(
            &hadc1,
            100
        ) != HAL_OK
    )
    {
        HAL_ADC_Stop(&hadc1);

        return 0;
    }

    uint16_t value =
        (uint16_t)HAL_ADC_GetValue(
            &hadc1
        );

    HAL_ADC_Stop(&hadc1);

    return value;
}


/* ============================================================
   PART 4 + PART 5 - SENSOR TASK

   Reads sensors every 2 seconds.

   Then packages readings into SensorData
   and sends the structure into sensorQueue.
   ============================================================ */

static void SensorTask(void *argument)
{
    (void)argument;

    TickType_t lastWakeTime =
        xTaskGetTickCount();

    const TickType_t sensorPeriod =
        pdMS_TO_TICKS(
            SENSOR_PERIOD_MS
        );


    for (;;)
    {
        SensorData data;

        data.temperature = 0.0f;
        data.humidity = 0.0f;
        data.lightLevel = 0;

        /*
         * PIR sensor is not implemented yet,
         * so motion is false for now.
         */
        data.motionDetected = false;


        /* ---------------- DHT22 ---------------- */

        bool dhtSuccess =
            DHT22_Read(
                &data.temperature,
                &data.humidity
            );


        /* ---------------- LDR ---------------- */

        uint16_t ldrRaw =
            LDR_ReadRaw();

        data.lightLevel =
            (int)(
                ((uint32_t)ldrRaw * 100UL)
                / 4095UL
            );


        /* ---------------- Serial diagnostic ---------------- */

        char buffer[180];


        if (dhtSuccess)
        {
            int temp10 =
                (int)(
                    data.temperature * 10.0f
                );

            int hum10 =
                (int)(
                    data.humidity * 10.0f
                );


            snprintf(
                buffer,
                sizeof(buffer),

                "SensorTask -> "
                "Temp: %d.%d C | "
                "Hum: %d.%d %% | "
                "Light: %d %%\r\n",

                temp10 / 10,

                temp10 < 0
                    ? -(temp10 % 10)
                    : temp10 % 10,

                hum10 / 10,
                hum10 % 10,

                data.lightLevel
            );
        }
        else
        {
            snprintf(
                buffer,
                sizeof(buffer),

                "SensorTask -> "
                "DHT22 read failed | "
                "Light: %d %%\r\n",

                data.lightLevel
            );
        }


        Log(buffer);


        /* ====================================================
           PART 5 - Send SensorData to queue
           ==================================================== */

        if (
            xQueueSend(
                sensorQueue,
                &data,
                0
            ) == pdPASS
        )
        {
            Log(
                "SensorTask -> data sent to queue\r\n"
            );
        }
        else
        {
            Log(
                "WARNING: sensor queue full\r\n"
            );
        }


        /* ====================================================
           Maintain fixed 2-second period
           ==================================================== */

        vTaskDelayUntil(
            &lastWakeTime,
            sensorPeriod
        );
    }
}


/* ============================================================
   PART 5 - QUEUE MONITOR TASK

   Waits for SensorData from sensorQueue.

   This proves inter-task communication using
   a FreeRTOS queue.
   ============================================================ */

static void QueueMonitorTask(void *argument)
{
    (void)argument;

    SensorData receivedData;


    for (;;)
    {
        if (
            xQueueReceive(
                sensorQueue,
                &receivedData,
                portMAX_DELAY
            ) == pdPASS
        )
        {
            int temp10 =
                (int)(
                    receivedData.temperature
                    * 10.0f
                );

            int hum10 =
                (int)(
                    receivedData.humidity
                    * 10.0f
                );


            char buffer[180];


            snprintf(
                buffer,
                sizeof(buffer),

                "Queue RX -> "
                "Temp: %d.%d C | "
                "Hum: %d.%d %% | "
                "Light: %d %% | "
                "Motion: %s\r\n",

                temp10 / 10,

                temp10 < 0
                    ? -(temp10 % 10)
                    : temp10 % 10,

                hum10 / 10,
                hum10 % 10,

                receivedData.lightLevel,

                receivedData.motionDetected
                    ? "YES"
                    : "NO"
            );


            Log(buffer);
        }
    }
}


/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    /* Vector table in Flash */

    SCB->VTOR =
        FLASH_BASE;


    /* STM32 HAL */

    HAL_Init();


    /* Hardware */

    LED_Init();

    UART1_Init();

    ADC1_Init();


    /* DHT22 */

    __HAL_RCC_GPIOB_CLK_ENABLE();

    DHT22_Init(
        GPIOB,
        GPIO_PIN_0
    );


    /* ========================================================
       Startup messages
       ======================================================== */

    Log(
        "\r\n"
        "BCA182 FreeRTOS Multisensor\r\n"
    );

    Log(
        "System starting...\r\n"
    );

    Log(
        "DHT22 initialized on PB0\r\n"
    );

    Log(
        "LDR initialized on PA0\r\n"
    );


    /*
     * Allow DHT22 to stabilize.
     */

    HAL_Delay(2000);


    /* ========================================================
       PART 5 - Create sensor queue

       Queue contains 5 SensorData structures.
       ======================================================== */

    sensorQueue =
        xQueueCreate(
            SENSOR_QUEUE_LENGTH,
            sizeof(SensorData)
        );


    if (sensorQueue == NULL)
    {
        Log(
            "ERROR: sensor queue creation failed\r\n"
        );

        while (1)
        {
        }
    }


    Log(
        "Sensor queue created successfully.\r\n"
    );


    /* ========================================================
       Create tasks
       ======================================================== */

    BaseType_t okA =
        xTaskCreate(
            TaskA,
            "TaskA",
            TASK_STACK_WORDS,
            NULL,
            TASK_A_PRIORITY,
            NULL
        );


    BaseType_t okB =
        xTaskCreate(
            TaskB,
            "TaskB",
            TASK_STACK_WORDS,
            NULL,
            TASK_B_PRIORITY,
            NULL
        );


    BaseType_t okSensor =
        xTaskCreate(
            SensorTask,
            "SensorTask",
            SENSOR_STACK_WORDS,
            NULL,
            SENSOR_TASK_PRIORITY,
            NULL
        );


    BaseType_t okQueue =
        xTaskCreate(
            QueueMonitorTask,
            "QueueMonitor",
            QUEUE_TASK_STACK_WORDS,
            NULL,
            QUEUE_TASK_PRIORITY,
            NULL
        );


    if (
        okA != pdPASS ||
        okB != pdPASS ||
        okSensor != pdPASS ||
        okQueue != pdPASS
    )
    {
        Log(
            "ERROR: task creation failed\r\n"
        );

        while (1)
        {
        }
    }


    Log(
        "Task A created successfully.\r\n"
    );

    Log(
        "Task B created successfully.\r\n"
    );

    Log(
        "SensorTask created successfully.\r\n"
    );

    Log(
        "QueueMonitorTask created successfully.\r\n"
    );


    /* ========================================================
       Start FreeRTOS
       ======================================================== */

    Log(
        "Starting scheduler...\r\n"
    );


    vTaskStartScheduler();


    /* Should never reach here */

    Log(
        "ERROR: scheduler failed to start\r\n"
    );


    while (1)
    {
    }
}