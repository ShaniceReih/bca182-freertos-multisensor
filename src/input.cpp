#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app_config.h"
#include "display_logic.h"
#include "input.h"
#include "logging.h"
#include "rtos_objects.h"
#include "system_state.h"

#define ENCODER_PORT       GPIOA
#define ENCODER_CLK_PIN    GPIO_PIN_1
#define ENCODER_DT_PIN     GPIO_PIN_2
#define ENCODER_SW_PIN     GPIO_PIN_3

static volatile int32_t encoderDelta = 0;

void Encoder_Init(void)
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

void InputTask(
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
