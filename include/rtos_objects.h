#ifndef RTOS_OBJECTS_H
#define RTOS_OBJECTS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"

#include "app_types.h"
#include "system_state.h"

extern QueueHandle_t displaySensorQueue;
extern QueueHandle_t alarmSensorQueue;
extern QueueHandle_t displayModeQueue;

extern SemaphoreHandle_t serialMutex;
extern volatile TaskHandle_t displayTaskHandle;

#define NOTIFY_EVENT_ACTIVE      (1UL << 0)
#define NOTIFY_EVENT_MOTION      (1UL << 1)
#define NOTIFY_EVENT_ALARM       (1UL << 2)
#define NOTIFY_EVENT_INACTIVE    (1UL << 3)

void SetMotionAndSystemState(bool motionDetected, SystemState state);
bool GetMotionDetected(void);
SystemState GetSystemState(void);

#endif
