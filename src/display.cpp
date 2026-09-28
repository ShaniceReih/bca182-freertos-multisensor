#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app_types.h"
#include "display.h"
#include "display_logic.h"
#include "logging.h"
#include "rtos_objects.h"
#include "ssd1306.h"
#include "system_state.h"

#define OLED_I2C_ADDRESS 0x3C

static I2C_HandleTypeDef hi2c1;

void I2C1_Init(void)
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

bool OLED_Detect(void)
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

bool OLED_ShowStartupScreen(void)
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

void DisplayTask(
    void *argument
)
{
    (void)argument;
    /*
     * Part X: capture this task's own handle after the scheduler starts.
     * This avoids changing the xTaskCreate() call that is already proven
     * stable in the Part XI build.
     */
    displayTaskHandle =
        xTaskGetCurrentTaskHandle();
    Log(
        "DisplayTask notification target ready.\r\n"
    );
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
        /*
         * Part X: consume any pending notification bits without blocking.
         * Existing queue/state behavior remains unchanged.
         */
        uint32_t notifiedEvents = 0;
        if (
            xTaskNotifyWait(
                0,
                0xFFFFFFFFUL,
                &notifiedEvents,
                0
            ) == pdTRUE
        )
        {
            if (
                (notifiedEvents & NOTIFY_EVENT_ACTIVE) != 0
            )
            {
                Log(
                    "DisplayTask notify -> EVENT_ACTIVE\r\n"
                );
            }
            if (
                (notifiedEvents & NOTIFY_EVENT_INACTIVE) != 0
            )
            {
                Log(
                    "DisplayTask notify -> EVENT_INACTIVE\r\n"
                );
            }
            if (
                (notifiedEvents & NOTIFY_EVENT_MOTION) != 0
            )
            {
                Log(
                    "DisplayTask notify -> EVENT_MOTION\r\n"
                );
            }
            if (
                (notifiedEvents & NOTIFY_EVENT_ALARM) != 0
            )
            {
                Log(
                    "DisplayTask notify -> EVENT_ALARM\r\n"
                );
            }
        }
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
