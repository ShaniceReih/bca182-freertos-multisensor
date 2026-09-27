#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

UART_HandleTypeDef huart1;

void SystemClock_Config(void);
static void MX_USART1_UART_Init(void);
extern "C" void app_main(void);

// Redirects printf() to send characters over USART1
extern "C" int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_USART1_UART_Init();

    printf("BCA182 FreeRTOS Multisensor\r\n");
    printf("System starting...\r\n");

    app_main();

    for (;;) { }
}

// --- FreeRTOS Application ---

static void TaskA(void *pvParameters) {
    for (;;) {
        printf("Task A running\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void TaskB(void *pvParameters) {
    for (;;) {
        printf("Task B running\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

extern "C" void app_main(void) {
    xTaskCreate(TaskA, "TaskA", 512, NULL, 2, NULL);
    xTaskCreate(TaskB, "TaskB", 512, NULL, 1, NULL);

    vTaskStartScheduler();
}

// --- STM32Cube Hardware Configuration Boilerplate ---

static void MX_USART1_UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

extern "C" void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(uartHandle->Instance==USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

// --- Safety net: fault + stack overflow handlers (kept from tonight's debugging) ---

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("STACK OVERFLOW in task: %s\r\n", pcTaskName);
    for (;;) { }
}

extern "C" {
    void HardFault_Handler(void) {
        printf("!!! HARD FAULT !!!\r\n");
        for (;;) { }
    }
    void MemManage_Handler(void) {
        printf("!!! MEM MANAGE FAULT !!!\r\n");
        for (;;) { }
    }
    void BusFault_Handler(void) {
        printf("!!! BUS FAULT !!!\r\n");
        for (;;) { }
    }
    void UsageFault_Handler(void) {
        printf("!!! USAGE FAULT !!!\r\n");
        for (;;) { }
    }
}