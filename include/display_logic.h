#ifndef DISPLAY_LOGIC_H
#define DISPLAY_LOGIC_H

#include "app_types.h"

DisplayMode nextDisplayMode(DisplayMode mode);
DisplayMode previousDisplayMode(DisplayMode mode);
const char *DisplayModeName(DisplayMode mode);

#endif
