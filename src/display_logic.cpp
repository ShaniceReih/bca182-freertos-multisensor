#include "display_logic.h"

DisplayMode nextDisplayMode(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return DisplayMode::HUMIDITY;
        case DisplayMode::HUMIDITY:
            return DisplayMode::LIGHT;
        case DisplayMode::LIGHT:
            return DisplayMode::MOTION;
        case DisplayMode::MOTION:
        default:
            return DisplayMode::TEMPERATURE;
    }
}

DisplayMode previousDisplayMode(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return DisplayMode::MOTION;
        case DisplayMode::HUMIDITY:
            return DisplayMode::TEMPERATURE;
        case DisplayMode::LIGHT:
            return DisplayMode::HUMIDITY;
        case DisplayMode::MOTION:
        default:
            return DisplayMode::LIGHT;
    }
}

const char *DisplayModeName(
    DisplayMode mode
)
{
    switch (mode)
    {
        case DisplayMode::TEMPERATURE:
            return "TEMPERATURE";
        case DisplayMode::HUMIDITY:
            return "HUMIDITY";
        case DisplayMode::LIGHT:
            return "LIGHT";
        case DisplayMode::MOTION:
            return "MOTION";
        default:
            return "UNKNOWN";
    }
}
