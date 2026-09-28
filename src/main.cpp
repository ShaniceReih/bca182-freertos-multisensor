#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "app_config.h"
#include "app_types.h"
#include "alarm_task.h"
#include "dht22.h"
#include "display.h"
#include "input.h"
#include "logging.h"
#include "motion.h"
#include "rtos_objects.h"
#include "sensors.h"

extern "C" void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

static void LED_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin =
        GPIO_PIN_13;
    gpio.Mode =
        GPIO_MODE_OUTPUT_PP;
    gpio.Speed =
        GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(
        GPIOC,
        &gpio
    );
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_13,
        GPIO_PIN_SET
    );
}

extern "C" void vAssertCalled(
    const char *file,
    int line
)
{
    __disable_irq();
    GPIOC->BSRR =
        (uint32_t)GPIO_PIN_13 << 16;
    RawPuts(
        "\r\nASSERT FAILED: "
    );
    RawPuts(
        file
    );
    RawPuts(
        " line "
    );
    RawPutNum(
        (uint32_t)line
    );
    RawPuts(
        "\r\n"
    );
    for (;;)
    {
    }
}

extern "C" void vApplicationStackOverflowHook(
    TaskHandle_t task,
    char *taskName
)
{
    (void)task;
    __disable_irq();
    RawPuts(
        "\r\nSTACK OVERFLOW in task: "
    );
    RawPuts(
        taskName
    );
    RawPuts(
        "\r\n"
    );
    for (;;)
    {
    }
}

extern "C" void HAL_TIM_PeriodElapsedCallback(
    TIM_HandleTypeDef *timer
)
{
    if (
        timer->Instance == TIM4 &&
        xTaskGetSchedulerState()
            == taskSCHEDULER_NOT_STARTED
    )
    {
        HAL_IncTick();
    }
}

extern "C" BaseType_t xPortConsumeTickYield(void);

extern "C" void vApplicationIdleHook(void)
{
    __WFI();
    if (
        xPortConsumeTickYield()
        != pdFALSE
    )
    {
        taskYIELD();
    }
}

int main(void)
{
    SCB->VTOR =
        FLASH_BASE;
    HAL_Init();
    /* ========================================================
       Hardware initialization
       ======================================================== */
    LED_Init();
    UART1_Init();
    ADC1_Init();
    I2C1_Init();
    Encoder_Init();
    Buzzer_Init();
    PIR_Init();
    /* ========================================================
       DHT22
       ======================================================== */
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
    Log(
        "Encoder CLK=PA1 DT=PA2 SW=PA3\r\n"
    );
    Log(
        "Buzzer PWM=PA8\r\n"
    );
    Log(
        "PIR OUT=PB1\r\n"
    );
    /* ========================================================
       OLED
       ======================================================== */
    Log(
        "Checking OLED at I2C address 0x3C...\r\n"
    );
    if (
        OLED_Detect()
    )
    {
        Log(
            "OLED detected at I2C address 0x3C!\r\n"
        );
        if (
            OLED_ShowStartupScreen()
        )
        {
            Log(
                "OLED initialized successfully.\r\n"
            );
        }
        else
        {
            Log(
                "ERROR: OLED initialization failed\r\n"
            );
        }
    }
    else
    {
        Log(
            "ERROR: OLED not detected\r\n"
        );
    }
    /*
     * Give DHT22 time to become ready.
     */
    HAL_Delay(
        2000
    );
    /* ========================================================
       Part XI - UART recursive mutex
       ======================================================== */
    serialMutex =
        xSemaphoreCreateRecursiveMutex();
    if (serialMutex == NULL)
    {
        Log(
            "ERROR: UART mutex creation failed\r\n"
        );
        while (1)
        {
        }
    }
    Log(
        "UART recursive mutex created successfully.\r\n"
    );
    Log(
        "Part X task notifications enabled for ACTIVE/MOTION/ALARM events.\r\n"
    );
    /* ========================================================
       Create queues
       ======================================================== */
    displaySensorQueue =
        xQueueCreate(
            1,
            sizeof(SensorData)
        );
    alarmSensorQueue =
        xQueueCreate(
            1,
            sizeof(SensorData)
        );
    displayModeQueue =
        xQueueCreate(
            1,
            sizeof(DisplayMode)
        );
    if (
        displaySensorQueue == NULL ||
        alarmSensorQueue == NULL ||
        displayModeQueue == NULL
    )
    {
        Log(
            "ERROR: queue creation failed\r\n"
        );
        while (1)
        {
        }
    }
    Log(
        "Display sensor queue created.\r\n"
    );
    Log(
        "Alarm sensor queue created.\r\n"
    );
    Log(
        "Display mode queue created.\r\n"
    );
    /* ========================================================
       Initial display mode
       ======================================================== */
    DisplayMode initialMode =
        DisplayMode::TEMPERATURE;
    xQueueOverwrite(
        displayModeQueue,
        &initialMode
    );
    /* ========================================================
       Create the five meaningful application tasks
       ======================================================== */
    BaseType_t okSensor =
        xTaskCreate(
            SensorTask,
            "SensorTask",
            SENSOR_STACK_WORDS,
            NULL,
            SENSOR_TASK_PRIORITY,
            NULL
        );
    BaseType_t okDisplay =
        xTaskCreate(
            DisplayTask,
            "DisplayTask",
            DISPLAY_STACK_WORDS,
            NULL,
            DISPLAY_TASK_PRIORITY,
            NULL
        );
    BaseType_t okInput =
        xTaskCreate(
            InputTask,
            "InputTask",
            INPUT_STACK_WORDS,
            NULL,
            INPUT_TASK_PRIORITY,
            NULL
        );
    BaseType_t okAlarm =
        xTaskCreate(
            AlarmTask,
            "AlarmTask",
            ALARM_STACK_WORDS,
            NULL,
            ALARM_TASK_PRIORITY,
            NULL
        );
    BaseType_t okMotion =
        xTaskCreate(
            MotionTask,
            "MotionTask",
            MOTION_STACK_WORDS,
            NULL,
            MOTION_TASK_PRIORITY,
            NULL
        );
    if (
        okSensor != pdPASS ||
        okDisplay != pdPASS ||
        okInput != pdPASS ||
        okAlarm != pdPASS ||
        okMotion != pdPASS
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
        "SensorTask created successfully.\r\n"
    );
    Log(
        "DisplayTask created successfully.\r\n"
    );
    Log(
        "InputTask created successfully.\r\n"
    );
    Log(
        "AlarmTask created successfully.\r\n"
    );
    Log(
        "MotionTask created successfully.\r\n"
    );
    /* ========================================================
       Start FreeRTOS
       ======================================================== */
    Log(
        "Starting scheduler...\r\n"
    );
    vTaskStartScheduler();
    /*
     * We should never reach here.
     */
    Log(
        "ERROR: scheduler failed to start\r\n"
    );
    while (1)
    {
    }
}
