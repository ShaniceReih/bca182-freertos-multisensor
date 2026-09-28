#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app_types.h"
#include "app_config.h"
#include "dht22.h"
#include "logging.h"
#include "rtos_objects.h"
#include "sensors.h"

static ADC_HandleTypeDef hadc1;

void ADC1_Init(void)
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

void SensorTask(
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
