#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "logging.h"
#include "rtos_objects.h"

extern "C" void Error_Handler(void);

static UART_HandleTypeDef huart1;

void UART1_Init(void)
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

static bool LogMutexUsable(void)
{
    return (serialMutex != NULL) &&
           (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
}

void Log(const char *message)
{
    bool locked = false;
    if (LogMutexUsable())
    {
        locked =
            (xSemaphoreTakeRecursive(
                serialMutex,
                portMAX_DELAY
            ) == pdTRUE);
    }
    HAL_UART_Transmit(
        &huart1,
        (uint8_t *)message,
        (uint16_t)strlen(message),
        HAL_MAX_DELAY
    );
    if (locked)
    {
        (void)xSemaphoreGiveRecursive(
            serialMutex
        );
    }
}

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

void RawPuts(const char *text)
{
    while (*text)
    {
        RawPutc(*text++);
    }
}

void RawPutNum(uint32_t value)
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
