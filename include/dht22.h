#ifndef DHT22_H
#define DHT22_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void DHT22_Init(GPIO_TypeDef *port, uint16_t pin);
bool DHT22_Read(float *temperature, float *humidity);
void DHT22_DelayMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
