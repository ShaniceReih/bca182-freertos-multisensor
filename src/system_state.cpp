#include "system_state.h"

SystemState evaluateSystemState(
    bool motionDetected,
    uint32_t millisecondsSinceMotion,
    uint32_t inactivityTimeoutMs
)
{
    /*
     * Any detected motion immediately makes the system ACTIVE.
     */
    if (motionDetected)
    {
        return SystemState::ACTIVE;
    }

    /*
     * If there has been no motion for at least the configured
     * timeout period, the system becomes INACTIVE.
     */
    if (millisecondsSinceMotion >= inactivityTimeoutMs)
    {
        return SystemState::INACTIVE;
    }

    /*
     * The inactivity timeout has not expired yet.
     */
    return SystemState::ACTIVE;
}