#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>

enum class SystemState
{
    ACTIVE,
    INACTIVE
};

SystemState evaluateSystemState(
    bool motionDetected,
    uint32_t millisecondsSinceMotion,
    uint32_t inactivityTimeoutMs
);

#endif