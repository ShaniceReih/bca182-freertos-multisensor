#include <string.h>
#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "dht22.h"
#include "ssd1306.h"
#include "alarm.h"
#include "system_state.h"


/* ============================================================
   Hardware handles
   ============================================================ */

static UART_HandleTypeDef huart1;
static ADC_HandleTypeDef hadc1;
static I2C_HandleTypeDef hi2c1;
static TIM_HandleTypeDef htim1;


/* ============================================================
   Sensor data
   ============================================================ */

struct SensorData
{
    float temperature;
    float humidity;
    int lightLevel;
    bool motionDetected;
};


/* ============================================================
   Display modes
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

static QueueHandle_t displaySensorQueue = NULL;
static QueueHandle_t alarmSensorQueue = NULL;
static QueueHandle_t displayModeQueue = NULL;


/* ============================================================
   Pin definitions
   ============================================================ */

/* Rotary encoder */

#define ENCODER_PORT       GPIOA
#define ENCODER_CLK_PIN    GPIO_PIN_1
#define ENCODER_DT_PIN     GPIO_PIN_2
#define ENCODER_SW_PIN     GPIO_PIN_3


/* Buzzer */

#define BUZZER_PORT         GPIOA
#define BUZZER_PIN          GPIO_PIN_8


/* PIR motion sensor */

#define PIR_PORT            GPIOB
#define PIR_PIN             GPIO_PIN_1


/* OLED */

#define OLED_I2C_ADDRESS    0x3C


/* ============================================================
   Encoder interrupt accumulator
   ============================================================ */

static volatile int32_t encoderDelta = 0;


/* ============================================================
   Shared motion/system state
   ============================================================ */

static volatile bool latestMotionDetected = false;

static volatile SystemState currentSystemState =
    SystemState::ACTIVE;


/* ============================================================
   Task settings
   ============================================================ */

#define SENSOR_PERIOD_MS          2000
#define SENSOR_TASK_PRIORITY      3
#define SENSOR_STACK_WORDS        384


#define DISPLAY_TASK_PRIORITY     2
#define DISPLAY_STACK_WORDS       512


#define INPUT_TASK_PRIORITY       3
#define INPUT_STACK_WORDS         256
#define INPUT_POLL_MS             50


#define ALARM_TASK_PRIORITY       2
#define ALARM_STACK_WORDS         384


#define MOTION_TASK_PRIORITY      3
#define MOTION_STACK_WORDS        256
#define MOTION_POLL_MS            100


#define INACTIVITY_TIMEOUT_MS     15000UL


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
   Diagnostic LED
   PC13
   ============================================================ */

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


/* ============================================================
   UART1
   PA9  = TX
   PA10 = RX
   ============================================================ */

static void UART1_Init(void)
{
    huart1.Instance =
        USART1;

    huart1.Init.BaudRate =
        115200;

    huart1.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart1.Init.StopBits =
        UART_STOPBITS_1;

    huart1.Init.Parity =
        UART_PARITY_NONE;

    huart1.Init.Mode =
        UART_MODE_TX_RX;

    huart1.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart1.Init.OverSampling =
        UART_OVERSAMPLING_16;


    if (
        HAL_UART_Init(&huart1)
        != HAL_OK
    )
    {
        Error_Handler();
    }
}


/* ============================================================
   Logging
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
   Raw UART diagnostics
   ============================================================ */

static void RawPutc(char c)
{
    while (
        (USART1->SR & USART_SR_TXE)
        == 0
    )
    {
    }

    USART1->DR =
        (uint8_t)c;
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

    int index =
        10;

    number[11] =
        '\0';


    do
    {
        number[index--] =
            (char)(
                '0' +
                (value % 10)
            );

        value /=
            10;

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


/* ============================================================
   Stack overflow handler
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
   HAL timebase
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
   Shared motion/system-state helpers
   ============================================================ */

static void SetMotionAndSystemState(
    bool motionDetected,
    SystemState state
)
{
    taskENTER_CRITICAL();

    latestMotionDetected =
        motionDetected;

    currentSystemState =
        state;

    taskEXIT_CRITICAL();
}


static bool GetMotionDetected(void)
{
    bool motion;


    taskENTER_CRITICAL();

    motion =
        latestMotionDetected;

    taskEXIT_CRITICAL();


    return motion;
}


static SystemState GetSystemState(void)
{
    SystemState state;


    taskENTER_CRITICAL();

    state =
        currentSystemState;

    taskEXIT_CRITICAL();


    return state;
}


/* ============================================================
   ADC1 / LDR
   PA0
   ============================================================ */

static void ADC1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();


    GPIO_InitTypeDef gpio = {0};

    gpio.Pin =
        GPIO_PIN_0;

    gpio.Mode =
        GPIO_MODE_ANALOG;


    HAL_GPIO_Init(
        GPIOA,
        &gpio
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
        )
        != HAL_OK
    )
    {
        Log(
            "ERROR: ADC channel failed\r\n"
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


    /*
     * Wokwi Blue Pill clock workaround.
     */

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


    Log(
        message
    );


    Log(
        "I2C1 initialized successfully.\r\n"
    );
}


/* ============================================================
   Buzzer
   PA8 = TIM1 Channel 1
   ============================================================ */

static void Buzzer_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();


    GPIO_InitTypeDef gpio = {0};

    gpio.Pin =
        BUZZER_PIN;

    gpio.Mode =
        GPIO_MODE_AF_PP;

    gpio.Speed =
        GPIO_SPEED_FREQ_HIGH;


    HAL_GPIO_Init(
        BUZZER_PORT,
        &gpio
    );


    uint32_t timerClock =
        HAL_RCC_GetPCLK2Freq();


    if (
        (RCC->CFGR & RCC_CFGR_PPRE2)
        != 0
    )
    {
        timerClock *=
            2;
    }


    uint32_t prescaler =
        timerClock / 1000000UL;


    if (
        prescaler == 0
    )
    {
        prescaler =
            1;
    }


    htim1.Instance =
        TIM1;

    htim1.Init.Prescaler =
        prescaler - 1;

    htim1.Init.CounterMode =
        TIM_COUNTERMODE_UP;

    htim1.Init.Period =
        999;

    htim1.Init.ClockDivision =
        TIM_CLOCKDIVISION_DIV1;

    htim1.Init.RepetitionCounter =
        0;


    if (
        HAL_TIM_PWM_Init(&htim1)
        != HAL_OK
    )
    {
        Log(
            "ERROR: buzzer timer initialization failed\r\n"
        );

        while (1)
        {
        }
    }


    TIM_OC_InitTypeDef channel = {0};

    channel.OCMode =
        TIM_OCMODE_PWM1;

    channel.Pulse =
        0;

    channel.OCPolarity =
        TIM_OCPOLARITY_HIGH;

    channel.OCFastMode =
        TIM_OCFAST_DISABLE;


    if (
        HAL_TIM_PWM_ConfigChannel(
            &htim1,
            &channel,
            TIM_CHANNEL_1
        )
        != HAL_OK
    )
    {
        Log(
            "ERROR: buzzer PWM configuration failed\r\n"
        );

        while (1)
        {
        }
    }


    if (
        HAL_TIM_PWM_Start(
            &htim1,
            TIM_CHANNEL_1
        )
        != HAL_OK
    )
    {
        Log(
            "ERROR: buzzer PWM start failed\r\n"
        );

        while (1)
        {
        }
    }


    __HAL_TIM_SET_COMPARE(
        &htim1,
        TIM_CHANNEL_1,
        0
    );


    Log(
        "Buzzer initialized on PA8.\r\n"
    );
}


static void Buzzer_SetAlarm(
    bool enabled
)
{
    if (enabled)
    {
        __HAL_TIM_SET_COMPARE(
            &htim1,
            TIM_CHANNEL_1,
            500
        );
    }
    else
    {
        __HAL_TIM_SET_COMPARE(
            &htim1,
            TIM_CHANNEL_1,
            0
        );
    }
}


/* ============================================================
   PIR
   PB1
   ============================================================ */

static void PIR_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();


    GPIO_InitTypeDef gpio = {0};

    gpio.Pin =
        PIR_PIN;

    gpio.Mode =
        GPIO_MODE_INPUT;

    gpio.Pull =
        GPIO_PULLDOWN;


    HAL_GPIO_Init(
        PIR_PORT,
        &gpio
    );


    Log(
        "PIR initialized on PB1.\r\n"
    );
}


static bool PIR_Read(void)
{
    return (
        HAL_GPIO_ReadPin(
            PIR_PORT,
            PIR_PIN
        )
        == GPIO_PIN_SET
    );
}


/* ============================================================
   Rotary encoder
   ============================================================ */

static void Encoder_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();


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
   Encoder interrupt
   ============================================================ */

extern "C" void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(
        ENCODER_CLK_PIN
    );
}


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
            dt ==
            GPIO_PIN_SET
        )
        {
            encoderDelta++;
        }
        else
        {
            encoderDelta--;
        }
    }
}


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
   OLED
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
   Display navigation
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
   OLED rendering
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
        case DisplayMode::TEMPERATURE:
        {
            int temp10 =
                (int)(
                    data.temperature *
                    10.0f
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


        case DisplayMode::HUMIDITY:
        {
            int hum10 =
                (int)(
                    data.humidity *
                    10.0f
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

static void SensorTask(
    void *argument
)
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

        data.motionDetected =
            GetMotionDetected();


        bool dhtOK =
            DHT22_Read(
                &data.temperature,
                &data.humidity
            );


        uint16_t raw =
            LDR_ReadRaw();


        data.lightLevel =
            (int)(
                ((uint32_t)raw * 100UL)
                / 4095UL
            );


        char message[200];


        if (dhtOK)
        {
            int temp10 =
                (int)(
                    data.temperature *
                    10.0f
                );


            int hum10 =
                (int)(
                    data.humidity *
                    10.0f
                );


            snprintf(
                message,
                sizeof(message),

                "SensorTask -> "
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

                data.lightLevel,

                data.motionDetected
                    ? "YES"
                    : "NO"
            );
        }
        else
        {
            snprintf(
                message,
                sizeof(message),

                "SensorTask -> "
                "DHT22 read failed | "
                "Light: %d %% | "
                "Motion: %s\r\n",

                data.lightLevel,

                data.motionDetected
                    ? "YES"
                    : "NO"
            );
        }


        Log(
            message
        );


        /*
         * Latest sensor sample for DisplayTask.
         */

        xQueueOverwrite(
            displaySensorQueue,
            &data
        );


        /*
         * Only valid temperature readings go to AlarmTask.
         */

        if (dhtOK)
        {
            xQueueOverwrite(
                alarmSensorQueue,
                &data
            );
        }


        vTaskDelayUntil(
            &lastWakeTime,
            period
        );
    }
}


/* ============================================================
   MotionTask
   ============================================================ */

static void MotionTask(
    void *argument
)
{
    (void)argument;


    TickType_t lastWakeTime =
        xTaskGetTickCount();


    TickType_t lastMotionTime =
        xTaskGetTickCount();


    const TickType_t pollPeriod =
        pdMS_TO_TICKS(
            MOTION_POLL_MS
        );


    bool previousMotion =
        false;


    SystemState previousState =
        SystemState::ACTIVE;


    SetMotionAndSystemState(
        false,
        SystemState::ACTIVE
    );


    Log(
        "MotionTask started. System ACTIVE.\r\n"
    );


    for (;;)
    {
        bool motionDetected =
            PIR_Read();


        TickType_t now =
            xTaskGetTickCount();


        /*
         * Motion restarts the inactivity timer.
         */

        if (motionDetected)
        {
            lastMotionTime =
                now;
        }


        TickType_t elapsedTicks =
            now -
            lastMotionTime;


        uint32_t elapsedMs =
            (uint32_t)(
                elapsedTicks *
                portTICK_PERIOD_MS
            );


        SystemState newState =
            evaluateSystemState(
                motionDetected,
                elapsedMs,
                INACTIVITY_TIMEOUT_MS
            );


        SetMotionAndSystemState(
            motionDetected,
            newState
        );


        /*
         * Motion rising edge.
         */

        if (
            motionDetected &&
            !previousMotion
        )
        {
            Log(
                "MotionTask -> PIR motion detected\r\n"
            );
        }


        /*
         * State transition.
         */

        if (
            newState !=
            previousState
        )
        {
            if (
                newState ==
                SystemState::ACTIVE
            )
            {
                Log(
                    "MotionTask -> System state: ACTIVE\r\n"
                );
            }
            else
            {
                Log(
                    "MotionTask -> 15 seconds without motion\r\n"
                );


                Log(
                    "MotionTask -> System state: INACTIVE\r\n"
                );
            }


            previousState =
                newState;
        }


        previousMotion =
            motionDetected;


        vTaskDelayUntil(
            &lastWakeTime,
            pollPeriod
        );
    }
}


/* ============================================================
   InputTask
   ============================================================ */

static void InputTask(
    void *argument
)
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
        SystemState systemState =
            GetSystemState();


        int32_t movement =
            EncoderTakeDelta();


        GPIO_PinState button =
            HAL_GPIO_ReadPin(
                ENCODER_PORT,
                ENCODER_SW_PIN
            );


        /*
         * Ignore encoder while INACTIVE.
         */

        if (
            systemState ==
            SystemState::INACTIVE
        )
        {
            previousSW =
                button;


            vTaskDelay(
                pdMS_TO_TICKS(
                    INPUT_POLL_MS
                )
            );


            continue;
        }


        while (
            movement > 0
        )
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


        while (
            movement < 0
        )
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


        if (
            previousSW ==
                GPIO_PIN_SET &&
            button ==
                GPIO_PIN_RESET
        )
        {
            Log(
                "InputTask -> encoder button pressed\r\n"
            );
        }


        previousSW =
            button;


        vTaskDelay(
            pdMS_TO_TICKS(
                INPUT_POLL_MS
            )
        );
    }
}


/* ============================================================
   DisplayTask
   ============================================================ */

static void DisplayTask(
    void *argument
)
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


    SystemState previousSystemState =
        SystemState::ACTIVE;


    for (;;)
    {
        SensorData incomingData;


        if (
            xQueueReceive(
                displaySensorQueue,
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


            if (
                GetSystemState() ==
                SystemState::ACTIVE
            )
            {
                redraw =
                    true;
            }


            int temp10 =
                (int)(
                    latestData.temperature *
                    10.0f
                );


            int hum10 =
                (int)(
                    latestData.humidity *
                    10.0f
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


            Log(
                message
            );
        }


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


            if (
                GetSystemState() ==
                SystemState::ACTIVE
            )
            {
                redraw =
                    true;
            }


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


        SystemState systemState =
            GetSystemState();


        /*
         * ACTIVE / INACTIVE transition.
         */

        if (
            systemState !=
            previousSystemState
        )
        {
            if (
                systemState ==
                SystemState::INACTIVE
            )
            {
                SSD1306_Clear();

                SSD1306_UpdateScreen();


                Log(
                    "DisplayTask -> OLED blank (INACTIVE)\r\n"
                );


                redraw =
                    false;
            }
            else
            {
                Log(
                    "DisplayTask -> system ACTIVE, restoring OLED\r\n"
                );


                redraw =
                    true;
            }


            previousSystemState =
                systemState;
        }


        /*
         * OLED updates only while ACTIVE.
         */

        if (
            systemState ==
                SystemState::ACTIVE &&
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
   AlarmTask
   ============================================================ */

static void AlarmTask(
    void *argument
)
{
    (void)argument;


    SensorData data;


    Buzzer_SetAlarm(
        false
    );


    Log(
        "AlarmTask started.\r\n"
    );


    for (;;)
    {
        if (
            xQueueReceive(
                alarmSensorQueue,
                &data,
                portMAX_DELAY
            )
            == pdPASS
        )
        {
            AlarmState state =
                evaluateTemperature(
                    data.temperature
                );


            bool alarmActive =
                (
                    state !=
                    AlarmState::NORMAL
                );


            Buzzer_SetAlarm(
                alarmActive
            );


            int temp10 =
                (int)(
                    data.temperature *
                    10.0f
                );


            char message[140];


            if (
                state ==
                AlarmState::LOW_TEMPERATURE
            )
            {
                snprintf(
                    message,
                    sizeof(message),

                    "AlarmTask -> LOW TEMPERATURE "
                    "(%d.%d C) | Buzzer ON\r\n",

                    temp10 / 10,

                    temp10 < 0
                        ? -(temp10 % 10)
                        : temp10 % 10
                );
            }
            else if (
                state ==
                AlarmState::HIGH_TEMPERATURE
            )
            {
                snprintf(
                    message,
                    sizeof(message),

                    "AlarmTask -> HIGH TEMPERATURE "
                    "(%d.%d C) | Buzzer ON\r\n",

                    temp10 / 10,

                    temp10 < 0
                        ? -(temp10 % 10)
                        : temp10 % 10
                );
            }
            else
            {
                snprintf(
                    message,
                    sizeof(message),

                    "AlarmTask -> NORMAL "
                    "(%d.%d C) | Buzzer OFF\r\n",

                    temp10 / 10,

                    temp10 < 0
                        ? -(temp10 % 10)
                        : temp10 % 10
                );
            }


            Log(
                message
            );
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