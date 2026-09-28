#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "logging.h"
#include "motion.h"
#include "rtos_objects.h"
#include "system_state.h"

#define PIR_PORT GPIOB
#define PIR_PIN  GPIO_PIN_1

void PIR_Init(void)
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

void MotionTask(
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
            if (displayTaskHandle != NULL)
            {
                xTaskNotify(
                    displayTaskHandle,
                    NOTIFY_EVENT_MOTION,
                    eSetBits
                );
            }
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
                if (displayTaskHandle != NULL)
                {
                    xTaskNotify(
                        displayTaskHandle,
                        NOTIFY_EVENT_ACTIVE,
                        eSetBits
                    );
                }
                Log(
                    "MotionTask -> System state: ACTIVE\r\n"
                );
            }
            else
            {
                if (displayTaskHandle != NULL)
                {
                    xTaskNotify(
                        displayTaskHandle,
                        NOTIFY_EVENT_INACTIVE,
                        eSetBits
                    );
                }
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
