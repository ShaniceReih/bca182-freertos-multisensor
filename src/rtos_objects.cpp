#include "rtos_objects.h"

QueueHandle_t displaySensorQueue = NULL;
QueueHandle_t alarmSensorQueue = NULL;
QueueHandle_t displayModeQueue = NULL;

SemaphoreHandle_t serialMutex = NULL;
volatile TaskHandle_t displayTaskHandle = NULL;

static volatile bool latestMotionDetected = false;
static volatile SystemState currentSystemState = SystemState::ACTIVE;

void SetMotionAndSystemState(
    bool motionDetected,
    SystemState state
)
{
    taskENTER_CRITICAL();
    latestMotionDetected =
        motionDetected;
    currentSystemState =
        state;
    taskEXIT_CRITICAL();
}

bool GetMotionDetected(void)
{
    bool motion;
    taskENTER_CRITICAL();
    motion =
        latestMotionDetected;
    taskEXIT_CRITICAL();
    return motion;
}

SystemState GetSystemState(void)
{
    SystemState state;
    taskENTER_CRITICAL();
    state =
        currentSystemState;
    taskEXIT_CRITICAL();
    return state;
}
