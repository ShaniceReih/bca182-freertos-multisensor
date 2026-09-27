#include <string.h>
#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "dht22.h"


/* ============================================================
   Hardware handles
   ============================================================ */

static UART_HandleTypeDef huart1;
static ADC_HandleTypeDef hadc1;


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

    /* LED ON */
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
   HAL timebase

   TIM4 is used by HAL before FreeRTOS starts.

   After the scheduler starts, the custom Wokwi-compatible
   FreeRTOS port provides the RTOS timing.
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
   PART 3 - Task settings
   ============================================================ */

#define TASK_A_PERIOD_MS   1000
#define TASK_B_PERIOD_MS   1000

#define TASK_A_PRIORITY    1
#define TASK_B_PRIORITY    2

#define TASK_STACK_WORDS   256


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
   Used by the LDR
   ============================================================ */

static void ADC1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();


    /* PA0 = analog input */

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin =
        GPIO_PIN_0;

    GPIO_InitStruct.Mode =
        GPIO_MODE_ANALOG;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );


    /* ADC configuration */

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


    /* ADC Channel 0 = PA0 */

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


    /* STM32F1 ADC calibration */

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

   Range:
   0 to 4095
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
   MAIN
   ============================================================ */

int main(void)
{
    /* Vector table in Flash */

    SCB->VTOR = FLASH_BASE;


    /* STM32 HAL */

    HAL_Init();


    /* Hardware */

    LED_Init();

    UART1_Init();

    ADC1_Init();


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


    /* ========================================================
       PART 4 - DHT22 TEST
       PB0
       ======================================================== */

    __HAL_RCC_GPIOB_CLK_ENABLE();


    DHT22_Init(
        GPIOB,
        GPIO_PIN_0
    );


    Log(
        "DHT22 initialized on PB0\r\n"
    );


    /*
     * DHT22 needs a short startup delay
     * after power is applied.
     */

    HAL_Delay(2000);


    float temperature = 0.0f;

    float humidity = 0.0f;


    if (
        DHT22_Read(
            &temperature,
            &humidity
        )
    )
    {
        /*
         * Convert to tenths so printf
         * floating-point support is
         * not required.
         *
         * Example:
         * 25.4 -> 254
         */

        int temp10 =
            (int)(temperature * 10.0f);

        int hum10 =
            (int)(humidity * 10.0f);


        char buffer[100];


        snprintf(
            buffer,
            sizeof(buffer),

            "DHT22 -> Temperature: "
            "%d.%d C | "
            "Humidity: %d.%d %%\r\n",

            temp10 / 10,

            temp10 < 0
                ? -(temp10 % 10)
                : temp10 % 10,

            hum10 / 10,

            hum10 % 10
        );


        Log(buffer);
    }
    else
    {
        Log(
            "ERROR: DHT22 read failed\r\n"
        );
    }


    /* ========================================================
       PART 4 - LDR TEST
       PA0 / ADC1 Channel 0
       ======================================================== */

    uint16_t ldrRaw =
        LDR_ReadRaw();


    /*
     * Convert 12-bit ADC:
     *
     * 0    -> 0%
     * 4095 -> 100%
     */

    uint32_t lightPercent =
        ((uint32_t)ldrRaw * 100UL)
        / 4095UL;


    char ldrBuffer[80];


    snprintf(
        ldrBuffer,
        sizeof(ldrBuffer),

        "LDR -> Raw: %u | "
        "Light: %lu %%\r\n",

        ldrRaw,
        lightPercent
    );


    Log(ldrBuffer);


    /* ========================================================
       Create Part 3 FreeRTOS tasks
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


    if (
        okA != pdPASS ||
        okB != pdPASS
    )
    {
        Log(
            "ERROR: task creation failed\r\n"
        );

        while (1)
        {
        }
    }


    /* ========================================================
       Start FreeRTOS
       ======================================================== */

    Log(
        "Starting scheduler...\r\n"
    );


    vTaskStartScheduler();


    /*
     * Should never reach here
     */

    Log(
        "ERROR: scheduler failed to start\r\n"
    );


    while (1)
    {
    }
}