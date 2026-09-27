#include <string.h>
#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "dht22.h"
#include "ssd1306.h"


/* ============================================================
   Hardware handles
   ============================================================ */

static UART_HandleTypeDef huart1;
static ADC_HandleTypeDef hadc1;
static I2C_HandleTypeDef hi2c1;


/* ============================================================
   PART 5 - Sensor data
   ============================================================ */

struct SensorData
{
    float temperature;
    float humidity;
    int lightLevel;
    bool motionDetected;
};


/* ============================================================
   PART 7 - Display pages
   ============================================================ */

enum class DisplayMode
{
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    MOTION
};


/* ============================================================
   FreeRTOS queues
   ============================================================ */

static QueueHandle_t sensorQueue = NULL;
static QueueHandle_t displayModeQueue = NULL;


/* ============================================================
   Encoder pins

   CLK -> PA1
   DT  -> PA2
   SW  -> PA3
   ============================================================ */

#define ENCODER_PORT       GPIOA
#define ENCODER_CLK_PIN    GPIO_PIN_1
#define ENCODER_DT_PIN     GPIO_PIN_2
#define ENCODER_SW_PIN     GPIO_PIN_3


/*
 * Encoder movement captured by interrupt.
 *
 * positive = clockwise
 * negative = counterclockwise
 */
static volatile int32_t encoderDelta = 0;


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

    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(
        GPIOC,
        &gpio
    );

    /* Blue Pill LED is active-low */
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_13,
        GPIO_PIN_SET
    );
}


/* ============================================================
   UART1
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

static void Log(const char *message)
{
    HAL_UART_Transmit(
        &huart1,
        (uint8_t *)message,
        (uint16_t)strlen(message),
        HAL_MAX_DELAY
    );
}


/* ============================================================
   Raw UART diagnostic functions
   ============================================================ */

static void RawPutc(char c)
{
    while (
        (USART1->SR & USART_SR_TXE) == 0
    )
    {
    }

    USART1->DR = (uint8_t)c;
}


static void RawPuts(const char *text)
{
    while (*text)
    {
        RawPutc(*text++);
    }
}


static void RawPutNum(uint32_t value)
{
    char number[12];

    int index = 10;

    number[11] = '\0';

    do
    {
        number[index--] =
            (char)(
                '0' + (value % 10)
            );

        value /= 10;

    } while (
        value > 0 &&
        index >= 0
    );

    RawPuts(
        &number[index + 1]
    );
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

    RawPuts(
        "\r\nASSERT FAILED: "
    );

    RawPuts(file);

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


/* ============================================================
   FreeRTOS stack overflow handler
   ============================================================ */

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


/* ============================================================
   HAL timebase support
   ============================================================ */

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


/* ============================================================
   Wokwi-compatible FreeRTOS support
   ============================================================ */

extern "C" BaseType_t
xPortConsumeTickYield(void);


/* ============================================================
   Idle hook
   ============================================================ */

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


/* ============================================================
   Task configuration
   ============================================================ */

#define TASK_A_PERIOD_MS       1000
#define TASK_B_PERIOD_MS       1000

#define TASK_A_PRIORITY        1
#define TASK_B_PRIORITY        2

#define TASK_STACK_WORDS       256


#define SENSOR_PERIOD_MS       2000
#define SENSOR_TASK_PRIORITY   3
#define SENSOR_STACK_WORDS     384


#define DISPLAY_TASK_PRIORITY  2
#define DISPLAY_STACK_WORDS    512


#define INPUT_TASK_PRIORITY    3
#define INPUT_STACK_WORDS      256

#define INPUT_POLL_MS          50


#define SENSOR_QUEUE_LENGTH    5

#define OLED_I2C_ADDRESS       0x3C


/* ============================================================
   Task A
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

        Log(
            "Task A running\r\n"
        );

        vTaskDelay(
            pdMS_TO_TICKS(
                TASK_A_PERIOD_MS
            )
        );
    }
}


/* ============================================================
   Task B
   ============================================================ */

static void TaskB(void *argument)
{
    (void)argument;

    for (;;)
    {
        Log(
            "Task B running\r\n"
        );

        vTaskDelay(
            pdMS_TO_TICKS(
                TASK_B_PERIOD_MS
            )
        );
    }
}


/* ============================================================
   ADC / LDR
   PA0 = ADC1 Channel 0
   ============================================================ */

static void ADC1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;

    HAL_GPIO_Init(
        GPIOA,
        &gpio
    );


    hadc1.Instance = ADC1;

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


    if (
        HAL_ADC_Init(&hadc1)
        != HAL_OK
    )
    {
        Log(
            "ERROR: ADC initialization failed\r\n"
        );

        while (1)
        {
        }
    }


    ADC_ChannelConfTypeDef channel =
        {0};

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
        )
        != HAL_OK
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
        )
        != HAL_OK
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
   Read LDR
   ============================================================ */

static uint16_t LDR_ReadRaw(void)
{
    if (
        HAL_ADC_Start(&hadc1)
        != HAL_OK
    )
    {
        return 0;
    }


    if (
        HAL_ADC_PollForConversion(
            &hadc1,
            100
        )
        != HAL_OK
    )
    {
        HAL_ADC_Stop(
            &hadc1
        );

        return 0;
    }


    uint16_t value =
        (uint16_t)
        HAL_ADC_GetValue(
            &hadc1
        );


    HAL_ADC_Stop(
        &hadc1
    );


    return value;
}


/* ============================================================
   I2C1
   PB6 = SCL
   PB7 = SDA
   ============================================================ */

static void I2C1_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();


    /* Wokwi clock workaround */
    RCC->CFGR &=
        ~RCC_CFGR_PPRE1;

    __DSB();
    __ISB();


    GPIO_InitTypeDef gpio = {0};

    gpio.Pin =
        GPIO_PIN_6 |
        GPIO_PIN_7;

    gpio.Mode =
        GPIO_MODE_AF_OD;

    gpio.Speed =
        GPIO_SPEED_FREQ_HIGH;


    HAL_GPIO_Init(
        GPIOB,
        &gpio
    );


    __HAL_RCC_I2C1_FORCE_RESET();

    __NOP();

    __HAL_RCC_I2C1_RELEASE_RESET();


    hi2c1.Instance =
        I2C1;

    hi2c1.Init.ClockSpeed =
        100000;

    hi2c1.Init.DutyCycle =
        I2C_DUTYCYCLE_2;

    hi2c1.Init.OwnAddress1 =
        0;

    hi2c1.Init.AddressingMode =
        I2C_ADDRESSINGMODE_7BIT;

    hi2c1.Init.DualAddressMode =
        I2C_DUALADDRESS_DISABLE;

    hi2c1.Init.OwnAddress2 =
        0;

    hi2c1.Init.GeneralCallMode =
        I2C_GENERALCALL_DISABLE;

    hi2c1.Init.NoStretchMode =
        I2C_NOSTRETCH_DISABLE;


    if (
        HAL_I2C_Init(&hi2c1)
        != HAL_OK
    )
    {
        Log(
            "ERROR: I2C initialization failed\r\n"
        );

        while (1)
        {
        }
    }


    char message[80];

    snprintf(
        message,
        sizeof(message),

        "PCLK1 for I2C = %lu Hz\r\n",

        HAL_RCC_GetPCLK1Freq()
    );

    Log(message);

    Log(
        "I2C1 initialized successfully.\r\n"
    );
}


/* ============================================================
   Rotary encoder initialization

   CLK uses EXTI falling-edge interrupt.
   DT and SW are normal inputs.
   ============================================================ */

static void Encoder_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();


    /* --------------------------------------------------------
       CLK = PA1
       Interrupt on falling edge
       -------------------------------------------------------- */

    GPIO_InitTypeDef clkGPIO = {0};

    clkGPIO.Pin =
        ENCODER_CLK_PIN;

    clkGPIO.Mode =
        GPIO_MODE_IT_FALLING;

    clkGPIO.Pull =
        GPIO_PULLUP;


    HAL_GPIO_Init(
        ENCODER_PORT,
        &clkGPIO
    );


    /* --------------------------------------------------------
       DT + SW
       Normal pull-up inputs
       -------------------------------------------------------- */

    GPIO_InitTypeDef inputGPIO = {0};

    inputGPIO.Pin =
        ENCODER_DT_PIN |
        ENCODER_SW_PIN;

    inputGPIO.Mode =
        GPIO_MODE_INPUT;

    inputGPIO.Pull =
        GPIO_PULLUP;


    HAL_GPIO_Init(
        ENCODER_PORT,
        &inputGPIO
    );


    /* --------------------------------------------------------
       Enable EXTI1 interrupt
       -------------------------------------------------------- */

    HAL_NVIC_SetPriority(
        EXTI1_IRQn,
        6,
        0
    );

    HAL_NVIC_EnableIRQ(
        EXTI1_IRQn
    );


    Log(
        "Rotary encoder initialized with EXTI on PA1\r\n"
    );
}


/* ============================================================
   EXTI1 IRQ Handler

   PA1 is EXTI line 1.
   ============================================================ */

extern "C" void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(
        ENCODER_CLK_PIN
    );
}


/* ============================================================
   Encoder interrupt callback

   Called immediately when CLK falls.

   Wokwi KY-040 direction:
   DT HIGH at CLK falling -> clockwise
   DT LOW  at CLK falling -> counterclockwise
   ============================================================ */

extern "C" void HAL_GPIO_EXTI_Callback(
    uint16_t GPIO_Pin
)
{
    if (
        GPIO_Pin ==
        ENCODER_CLK_PIN
    )
    {
        GPIO_PinState dt =
            HAL_GPIO_ReadPin(
                ENCODER_PORT,
                ENCODER_DT_PIN
            );


        if (
            dt == GPIO_PIN_SET
        )
        {
            /*
             * Clockwise
             */
            encoderDelta++;
        }
        else
        {
            /*
             * Counterclockwise
             */
            encoderDelta--;
        }
    }
}


/* ============================================================
   Safely take accumulated encoder movement.

   Interrupts are disabled only for a few instructions.
   ============================================================ */

static int32_t EncoderTakeDelta(void)
{
    uint32_t previousPrimask =
        __get_PRIMASK();


    __disable_irq();


    int32_t delta =
        encoderDelta;


    encoderDelta =
        0;


    __set_PRIMASK(
        previousPrimask
    );


    return delta;
}


/* ============================================================
   OLED detection
   ============================================================ */

static bool OLED_Detect(void)
{
    return (
        HAL_I2C_IsDeviceReady(
            &hi2c1,
            OLED_I2C_ADDRESS << 1,
            3,
            100
        )
        == HAL_OK
    );
}


/* ============================================================
   OLED initial page
   ============================================================ */

static bool OLED_ShowStartupScreen(void)
{
    if (
        !SSD1306_Init(
            &hi2c1
        )
    )
    {
        return false;
    }


    SSD1306_Clear();


    SSD1306_SetCursor(
        28,
        2
    );


    SSD1306_WriteString(
        "ROOM MONITOR"
    );


    SSD1306_SetCursor(
        31,
        4
    );


    SSD1306_WriteString(
        "TEMPERATURE"
    );


    SSD1306_UpdateScreen();


    return true;
}


/* ============================================================
   PART 7 - Navigation logic
   ============================================================ */

static DisplayMode nextDisplayMode(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return DisplayMode::HUMIDITY;

        case DisplayMode::HUMIDITY:
            return DisplayMode::LIGHT;

        case DisplayMode::LIGHT:
            return DisplayMode::MOTION;

        case DisplayMode::MOTION:
        default:
            return DisplayMode::TEMPERATURE;
    }
}


static DisplayMode previousDisplayMode(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return DisplayMode::MOTION;

        case DisplayMode::HUMIDITY:
            return DisplayMode::TEMPERATURE;

        case DisplayMode::LIGHT:
            return DisplayMode::HUMIDITY;

        case DisplayMode::MOTION:
        default:
            return DisplayMode::LIGHT;
    }
}


/* ============================================================
   Display mode name
   ============================================================ */

static const char *DisplayModeName(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return "TEMPERATURE";

        case DisplayMode::HUMIDITY:
            return "HUMIDITY";

        case DisplayMode::LIGHT:
            return "LIGHT";

        case DisplayMode::MOTION:
            return "MOTION";

        default:
            return "UNKNOWN";
    }
}


/* ============================================================
   OLED selected-page rendering
   ============================================================ */

static void OLED_RenderMode(
    DisplayMode mode,
    const SensorData &data
)
{
    SSD1306_Clear();


    SSD1306_SetCursor(
        28,
        0
    );

    SSD1306_WriteString(
        "ROOM MONITOR"
    );


    char value[32];


    switch (mode)
    {
        /* ----------------------------------------------------
           TEMPERATURE
           ---------------------------------------------------- */

        case DisplayMode::TEMPERATURE:
        {
            int temp10 =
                (int)(
                    data.temperature
                    * 10.0f
                );


            SSD1306_SetCursor(
                31,
                2
            );

            SSD1306_WriteString(
                "TEMPERATURE"
            );


            snprintf(
                value,
                sizeof(value),

                "%d.%d C",

                temp10 / 10,

                temp10 < 0
                    ? -(temp10 % 10)
                    : temp10 % 10
            );


            SSD1306_SetCursor(
                46,
                4
            );

            SSD1306_WriteString(
                value
            );

            break;
        }


        /* ----------------------------------------------------
           HUMIDITY
           ---------------------------------------------------- */

        case DisplayMode::HUMIDITY:
        {
            int hum10 =
                (int)(
                    data.humidity
                    * 10.0f
                );


            SSD1306_SetCursor(
                40,
                2
            );

            SSD1306_WriteString(
                "HUMIDITY"
            );


            snprintf(
                value,
                sizeof(value),

                "%d.%d %%",
                hum10 / 10,
                hum10 % 10
            );


            SSD1306_SetCursor(
                46,
                4
            );

            SSD1306_WriteString(
                value
            );

            break;
        }


        /* ----------------------------------------------------
           LIGHT
           ---------------------------------------------------- */

        case DisplayMode::LIGHT:
        {
            SSD1306_SetCursor(
                49,
                2
            );

            SSD1306_WriteString(
                "LIGHT"
            );


            snprintf(
                value,
                sizeof(value),

                "%d %%",
                data.lightLevel
            );


            SSD1306_SetCursor(
                49,
                4
            );

            SSD1306_WriteString(
                value
            );

            break;
        }


        /* ----------------------------------------------------
           MOTION
           ---------------------------------------------------- */

        case DisplayMode::MOTION:
        default:
        {
            SSD1306_SetCursor(
                46,
                2
            );

            SSD1306_WriteString(
                "MOTION"
            );


            SSD1306_SetCursor(
                52,
                4
            );


            SSD1306_WriteString(
                data.motionDetected
                    ? "YES"
                    : "NO"
            );

            break;
        }
    }


    SSD1306_UpdateScreen();
}


/* ============================================================
   SensorTask
   ============================================================ */

static void SensorTask(void *argument)
{
    (void)argument;


    TickType_t lastWakeTime =
        xTaskGetTickCount();


    const TickType_t period =
        pdMS_TO_TICKS(
            SENSOR_PERIOD_MS
        );


    for (;;)
    {
        SensorData data;


        data.temperature =
            0.0f;

        data.humidity =
            0.0f;

        data.lightLevel =
            0;

        /*
         * PIR comes in Part IX.
         */
        data.motionDetected =
            false;


        /* ----------------------------------------------------
           DHT22
           ---------------------------------------------------- */

        bool dhtOK =
            DHT22_Read(
                &data.temperature,
                &data.humidity
            );


        /* ----------------------------------------------------
           LDR
           ---------------------------------------------------- */

        uint16_t raw =
            LDR_ReadRaw();


        data.lightLevel =
            (int)(
                ((uint32_t)raw * 100UL)
                / 4095UL
            );


        /* ----------------------------------------------------
           Serial diagnostic
           ---------------------------------------------------- */

        char message[180];


        if (dhtOK)
        {
            int temp10 =
                (int)(
                    data.temperature
                    * 10.0f
                );


            int hum10 =
                (int)(
                    data.humidity
                    * 10.0f
                );


            snprintf(
                message,
                sizeof(message),

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
                message,
                sizeof(message),

                "SensorTask -> "
                "DHT22 read failed | "
                "Light: %d %%\r\n",

                data.lightLevel
            );
        }


        Log(message);


        /* ----------------------------------------------------
           Queue data to DisplayTask
           ---------------------------------------------------- */

        if (
            xQueueSend(
                sensorQueue,
                &data,
                0
            )
            == pdPASS
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


        vTaskDelayUntil(
            &lastWakeTime,
            period
        );
    }
}


/* ============================================================
   PART 7 - InputTask

   Interrupt captures the fast physical encoder pulse.
   InputTask performs the actual navigation logic.
   ============================================================ */

static void InputTask(void *argument)
{
    (void)argument;


    DisplayMode currentMode =
        DisplayMode::TEMPERATURE;


    GPIO_PinState previousSW =
        HAL_GPIO_ReadPin(
            ENCODER_PORT,
            ENCODER_SW_PIN
        );


    Log(
        "InputTask started.\r\n"
    );


    for (;;)
    {
        /* ----------------------------------------------------
           Get movement captured by EXTI interrupt
           ---------------------------------------------------- */

        int32_t movement =
            EncoderTakeDelta();


        /* ----------------------------------------------------
           Clockwise movement
           ---------------------------------------------------- */

        while (movement > 0)
        {
            currentMode =
                nextDisplayMode(
                    currentMode
                );


            Log(
                "InputTask -> clockwise -> "
            );


            Log(
                DisplayModeName(
                    currentMode
                )
            );


            Log(
                "\r\n"
            );


            xQueueOverwrite(
                displayModeQueue,
                &currentMode
            );


            movement--;
        }


        /* ----------------------------------------------------
           Counterclockwise movement
           ---------------------------------------------------- */

        while (movement < 0)
        {
            currentMode =
                previousDisplayMode(
                    currentMode
                );


            Log(
                "InputTask -> counterclockwise -> "
            );


            Log(
                DisplayModeName(
                    currentMode
                )
            );


            Log(
                "\r\n"
            );


            xQueueOverwrite(
                displayModeQueue,
                &currentMode
            );


            movement++;
        }


        /* ----------------------------------------------------
           Encoder button
           ---------------------------------------------------- */

        GPIO_PinState button =
            HAL_GPIO_ReadPin(
                ENCODER_PORT,
                ENCODER_SW_PIN
            );


        if (
            previousSW == GPIO_PIN_SET &&
            button == GPIO_PIN_RESET
        )
        {
            Log(
                "InputTask -> encoder button pressed\r\n"
            );
        }


        previousSW =
            button;


        /*
         * InputTask still blocks normally.
         * The interrupt only records fast encoder edges.
         */
        vTaskDelay(
            pdMS_TO_TICKS(
                INPUT_POLL_MS
            )
        );
    }
}


/* ============================================================
   DisplayTask

   Only this task owns/writes to the OLED.
   ============================================================ */

static void DisplayTask(void *argument)
{
    (void)argument;


    SensorData latestData =
    {
        0.0f,
        0.0f,
        0,
        false
    };


    DisplayMode currentMode =
        DisplayMode::TEMPERATURE;


    bool haveSensorData =
        false;


    bool redraw =
        true;


    for (;;)
    {
        /* ----------------------------------------------------
           Receive latest sensor data
           ---------------------------------------------------- */

        SensorData incomingData;


        if (
            xQueueReceive(
                sensorQueue,
                &incomingData,
                pdMS_TO_TICKS(50)
            )
            == pdPASS
        )
        {
            latestData =
                incomingData;

            haveSensorData =
                true;

            redraw =
                true;


            int temp10 =
                (int)(
                    latestData.temperature
                    * 10.0f
                );


            int hum10 =
                (int)(
                    latestData.humidity
                    * 10.0f
                );


            char message[180];


            snprintf(
                message,
                sizeof(message),

                "DisplayTask RX -> "
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

                latestData.lightLevel,

                latestData.motionDetected
                    ? "YES"
                    : "NO"
            );


            Log(message);
        }


        /* ----------------------------------------------------
           Receive latest selected page
           ---------------------------------------------------- */

        DisplayMode newMode;


        if (
            xQueueReceive(
                displayModeQueue,
                &newMode,
                0
            )
            == pdPASS
        )
        {
            currentMode =
                newMode;

            redraw =
                true;


            Log(
                "DisplayTask -> selected page: "
            );


            Log(
                DisplayModeName(
                    currentMode
                )
            );


            Log(
                "\r\n"
            );
        }


        /* ----------------------------------------------------
           OLED update
           ---------------------------------------------------- */

        if (
            redraw &&
            haveSensorData
        )
        {
            OLED_RenderMode(
                currentMode,
                latestData
            );


            redraw =
                false;
        }
    }
}


/* ============================================================
   MAIN
   ============================================================ */

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
     * Allow DHT22 to stabilize.
     */
    HAL_Delay(
        2000
    );


    /* ========================================================
       Create queues
       ======================================================== */

    sensorQueue =
        xQueueCreate(
            SENSOR_QUEUE_LENGTH,
            sizeof(SensorData)
        );


    displayModeQueue =
        xQueueCreate(
            1,
            sizeof(DisplayMode)
        );


    if (
        sensorQueue == NULL ||
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
        "Sensor queue created successfully.\r\n"
    );


    Log(
        "Display mode queue created successfully.\r\n"
    );


    /* Initial display page */

    DisplayMode initialMode =
        DisplayMode::TEMPERATURE;


    xQueueOverwrite(
        displayModeQueue,
        &initialMode
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


    if (
        okA != pdPASS ||
        okB != pdPASS ||
        okSensor != pdPASS ||
        okDisplay != pdPASS ||
        okInput != pdPASS
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
        "DisplayTask created successfully.\r\n"
    );


    Log(
        "InputTask created successfully.\r\n"
    );


    /* ========================================================
       Start FreeRTOS
       ======================================================== */

    Log(
        "Starting scheduler...\r\n"
    );


    vTaskStartScheduler();


    Log(
        "ERROR: scheduler failed to start\r\n"
    );


    while (1)
    {
    }
}