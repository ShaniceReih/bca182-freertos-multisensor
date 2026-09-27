#include "dht22.h"
#include "stm32f1xx_hal.h"


/* ============================================================
   DHT22 driver state
   ============================================================ */

static GPIO_TypeDef *dhtPort = nullptr;
static uint16_t dhtPin = 0;


/* ============================================================
   DWT microsecond timer
   ============================================================ */

static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    DWT->CYCCNT = 0;

    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}


static void DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;

    uint32_t ticks =
        (SystemCoreClock / 1000000UL) * us;

    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}


/* ============================================================
   GPIO helpers
   ============================================================ */

static void SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = dhtPin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(dhtPort, &gpio);
}


static void SetInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = dhtPin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(dhtPort, &gpio);
}


static inline GPIO_PinState ReadPin(void)
{
    return HAL_GPIO_ReadPin(
        dhtPort,
        dhtPin
    );
}


/* ============================================================
   Wait until the pin reaches a requested state.

   IMPORTANT:
   Every wait has a timeout, so the DHT22 can never trap
   SensorTask forever.
   ============================================================ */

static bool WaitForState(
    GPIO_PinState state,
    uint32_t timeoutUs
)
{
    uint32_t start = DWT->CYCCNT;

    uint32_t timeoutTicks =
        (SystemCoreClock / 1000000UL)
        * timeoutUs;


    while (ReadPin() != state)
    {
        if (
            (DWT->CYCCNT - start)
            >= timeoutTicks
        )
        {
            return false;
        }
    }


    return true;
}


/* ============================================================
   Measure how long the pin remains in a particular state.
   ============================================================ */

static bool MeasureState(
    GPIO_PinState state,
    uint32_t timeoutUs,
    uint32_t *durationUs
)
{
    uint32_t start = DWT->CYCCNT;

    uint32_t timeoutTicks =
        (SystemCoreClock / 1000000UL)
        * timeoutUs;


    while (ReadPin() == state)
    {
        if (
            (DWT->CYCCNT - start)
            >= timeoutTicks
        )
        {
            return false;
        }
    }


    uint32_t elapsed =
        DWT->CYCCNT - start;


    *durationUs =
        elapsed /
        (SystemCoreClock / 1000000UL);


    return true;
}


/* ============================================================
   Public initialization
   ============================================================ */

void DHT22_Init(
    GPIO_TypeDef *port,
    uint16_t pin
)
{
    dhtPort = port;
    dhtPin = pin;


    DWT_Init();


    SetOutput();


    HAL_GPIO_WritePin(
        dhtPort,
        dhtPin,
        GPIO_PIN_SET
    );
}


/* ============================================================
   Millisecond delay helper

   Uses DWT rather than HAL_Delay so it does not depend on
   FreeRTOS/HAL timer interaction.
   ============================================================ */

void DHT22_DelayMs(uint32_t ms)
{
    while (ms--)
    {
        DelayUs(1000);
    }
}


/* ============================================================
   Read DHT22

   IMPORTANT:
   Interrupt state is always restored before returning,
   including timeout/error paths.
   ============================================================ */

bool DHT22_Read(
    float *temperature,
    float *humidity
)
{
    if (
        dhtPort == nullptr ||
        temperature == nullptr ||
        humidity == nullptr
    )
    {
        return false;
    }


    uint8_t data[5] =
    {
        0,
        0,
        0,
        0,
        0
    };


    /* --------------------------------------------------------
       Send DHT22 start signal
       -------------------------------------------------------- */

    SetOutput();


    HAL_GPIO_WritePin(
        dhtPort,
        dhtPin,
        GPIO_PIN_RESET
    );


    /* DHT22 requires at least ~1 ms LOW */

    DelayUs(1200);


    HAL_GPIO_WritePin(
        dhtPort,
        dhtPin,
        GPIO_PIN_SET
    );


    DelayUs(30);


    SetInput();


    /*
     * Preserve the previous global interrupt state.
     *
     * The DHT protocol needs precise microsecond timing,
     * so interrupts are disabled only during the data frame.
     */

    uint32_t previousPrimask =
        __get_PRIMASK();


    __disable_irq();


    bool success = false;


    /* --------------------------------------------------------
       Sensor response:
       ~80 us LOW
       ~80 us HIGH
       -------------------------------------------------------- */

    if (
        !WaitForState(
            GPIO_PIN_RESET,
            150
        )
    )
    {
        goto cleanup;
    }


    if (
        !WaitForState(
            GPIO_PIN_SET,
            150
        )
    )
    {
        goto cleanup;
    }


    if (
        !WaitForState(
            GPIO_PIN_RESET,
            150
        )
    )
    {
        goto cleanup;
    }


    /* --------------------------------------------------------
       Read 40 bits
       -------------------------------------------------------- */

    for (int bit = 0; bit < 40; bit++)
    {
        /*
         * Every bit begins with an approximately
         * 50 us LOW pulse.
         */

        if (
            !WaitForState(
                GPIO_PIN_SET,
                100
            )
        )
        {
            goto cleanup;
        }


        /*
         * Measure HIGH pulse:
         *
         * ~26-28 us = 0
         * ~70 us    = 1
         */

        uint32_t highTimeUs = 0;


        if (
            !MeasureState(
                GPIO_PIN_SET,
                120,
                &highTimeUs
            )
        )
        {
            goto cleanup;
        }


        data[bit / 8] <<= 1;


        if (highTimeUs > 50)
        {
            data[bit / 8] |= 1;
        }
    }


    /* --------------------------------------------------------
       Check checksum
       -------------------------------------------------------- */

    {
        uint8_t checksum =
            (uint8_t)(
                data[0]
                + data[1]
                + data[2]
                + data[3]
            );


        if (checksum != data[4])
        {
            goto cleanup;
        }
    }


    /* --------------------------------------------------------
       Humidity
       -------------------------------------------------------- */

    {
        uint16_t rawHumidity =
            ((uint16_t)data[0] << 8)
            | data[1];


        *humidity =
            rawHumidity / 10.0f;
    }


    /* --------------------------------------------------------
       Temperature
       -------------------------------------------------------- */

    {
        uint16_t rawTemperature =
            ((uint16_t)(data[2] & 0x7F) << 8)
            | data[3];


        *temperature =
            rawTemperature / 10.0f;


        if ((data[2] & 0x80) != 0)
        {
            *temperature =
                -*temperature;
        }
    }


    success = true;


cleanup:

    /*
     * CRITICAL:
     * Restore interrupt state on EVERY exit path.
     *
     * This prevents a failed DHT read from permanently
     * stopping TIM3 / the FreeRTOS scheduler.
     */

    __set_PRIMASK(previousPrimask);


    SetOutput();


    HAL_GPIO_WritePin(
        dhtPort,
        dhtPin,
        GPIO_PIN_SET
    );


    return success;
}