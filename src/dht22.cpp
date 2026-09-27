#include "dht22.h"
#include <stdio.h>

static GPIO_TypeDef *dht_port;
static uint16_t dht_pin;

static void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline void delay_us(uint32_t us) {
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < ticks) { }
}

void DHT22_DelayMs(uint32_t ms) {
    delay_us(ms * 1000U);
}

// Explicitly switch the pin to a real output (driving low)
static void DHT22_SetOutput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = dht_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(dht_port, &GPIO_InitStruct);
}

// Explicitly switch the pin to a real input (so we can read the sensor driving it)
static void DHT22_SetInput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = dht_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(dht_port, &GPIO_InitStruct);
}

static inline GPIO_PinState DHT22_ReadPinState(void) {
    return HAL_GPIO_ReadPin(dht_port, dht_pin);
}

static bool WaitForState(GPIO_PinState state, uint32_t timeout_us) {
    uint32_t ticks = timeout_us * (SystemCoreClock / 1000000U);
    uint32_t start = DWT->CYCCNT;
    while (DHT22_ReadPinState() != state) {
        if ((DWT->CYCCNT - start) > ticks) {
            return false;
        }
    }
    return true;
}

void DHT22_Init(GPIO_TypeDef *port, uint16_t pin) {
    dht_port = port;
    dht_pin = pin;

    DWT_Init();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    DHT22_SetInput();
}

bool DHT22_Read(float *temperature, float *humidity) {
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // 1. Send start signal
    DHT22_SetOutput();
    HAL_GPIO_WritePin(dht_port, dht_pin, GPIO_PIN_RESET);
    delay_us(2000); // Let interrupts run normally during this long 2ms delay
    
    // --- START CRITICAL TIMING SECTION ---
    __disable_irq(); // Pause background tasks for microsecond precision

    HAL_GPIO_WritePin(dht_port, dht_pin, GPIO_PIN_SET);
    delay_us(30);
    DHT22_SetInput();

    // 2. Wait for the sensor's response pulse
    if (!WaitForState(GPIO_PIN_RESET, 150)) {
        __enable_irq(); // MUST re-enable before exiting on error!
        printf("DHT22: no response (step 1 - initial low)\r\n");
        return false;
    }
    if (!WaitForState(GPIO_PIN_SET, 150)) {
        __enable_irq(); 
        printf("DHT22: no response (step 2 - response high)\r\n");
        return false;
    }
    if (!WaitForState(GPIO_PIN_RESET, 150)) {
        __enable_irq(); 
        printf("DHT22: no response (step 3 - start of bit 0)\r\n");
        return false;
    }

    // 3. Read 40 data bits
    for (int i = 0; i < 40; i++) {
        if (!WaitForState(GPIO_PIN_SET, 150)) {
            __enable_irq(); 
            printf("DHT22: timeout waiting for bit %d high\r\n", i);
            return false;
        }

        uint32_t start = DWT->CYCCNT;
        if (!WaitForState(GPIO_PIN_RESET, 150)) {
            __enable_irq(); 
            printf("DHT22: timeout waiting for bit %d low\r\n", i);
            return false;
        }
        uint32_t elapsed_us = (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);

        data[i / 8] <<= 1;
        if (elapsed_us > 40) {
            data[i / 8] |= 1;
        }
    }

    // --- END CRITICAL TIMING SECTION ---
    __enable_irq(); // Turn interrupts back on immediately after all 40 bits are safely read

    // 4. Validate and calculate results
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        printf("DHT22: checksum mismatch (got %d, expected %d)\r\n", checksum, data[4]);
        return false;
    }

    *humidity = ((data[0] << 8) | data[1]) / 10.0f;

    uint16_t rawTemp = ((data[2] & 0x7F) << 8) | data[3];
    float temp = rawTemp / 10.0f;
    if (data[2] & 0x80) {
        temp = -temp;
    }
    *temperature = temp;

    return true;
}
