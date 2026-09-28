#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "alarm.h"
#include "alarm_task.h"
#include "app_types.h"
#include "logging.h"
#include "rtos_objects.h"

#define BUZZER_PORT GPIOA
#define BUZZER_PIN  GPIO_PIN_8

static TIM_HandleTypeDef htim1;

void Buzzer_Init(void)
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

void AlarmTask(
    void *argument
)
{
    (void)argument;
    SensorData data;
    bool previousAlarmActive =
        false;
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
            /*
             * Part X alarm notification:
             * notify DisplayTask only when the alarm becomes ACTIVE.
             *
             * Returning to NORMAL is already represented by AlarmTask's
             * state, buzzer OFF behavior, and serial diagnostic. Keeping
             * the notification edge one-way avoids false/duplicate
             * EVENT_ALARM messages while the temperature is normal.
             */
            if (
                alarmActive &&
                !previousAlarmActive
            )
            {
                if (displayTaskHandle != NULL)
                {
                    xTaskNotify(
                        displayTaskHandle,
                        NOTIFY_EVENT_ALARM,
                        eSetBits
                    );
                }
            }

            previousAlarmActive =
                alarmActive;
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
