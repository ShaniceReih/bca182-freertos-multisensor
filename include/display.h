#ifndef DISPLAY_H
#define DISPLAY_H

void I2C1_Init(void);
bool OLED_Detect(void);
bool OLED_ShowStartupScreen(void);
void DisplayTask(void *argument);

#endif
