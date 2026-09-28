#include "alarm.h"

AlarmState evaluateTemperature(float temperature)
{
    /*
     * Laboratory limits:
     *
     * temperature < 18 C  -> LOW
     * 18 C to 30 C        -> NORMAL
     * temperature > 30 C  -> HIGH
     *
     * Therefore exactly 18 C and exactly 30 C
     * are still considered NORMAL.
     */

    if (temperature < 18.0f)
    {
        return AlarmState::LOW_TEMPERATURE;
    }

    if (temperature > 30.0f)
    {
        return AlarmState::HIGH_TEMPERATURE;
    }

    return AlarmState::NORMAL;
}