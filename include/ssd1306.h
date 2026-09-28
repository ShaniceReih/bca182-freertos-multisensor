#ifndef SSD1306_H
#define SSD1306_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_ADDRESS     0x3C

bool SSD1306_Init(I2C_HandleTypeDef *hi2c);

void SSD1306_Clear(void);
void SSD1306_UpdateScreen(void);

void SSD1306_SetCursor(
    uint8_t x,
    uint8_t page
);

void SSD1306_WriteChar(char c);
void SSD1306_WriteString(const char *text);

#ifdef __cplusplus
}
#endif

#endif