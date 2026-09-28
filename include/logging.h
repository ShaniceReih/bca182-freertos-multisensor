#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>

void UART1_Init(void);
void Log(const char *message);
void RawPuts(const char *text);
void RawPutNum(uint32_t value);

#endif
