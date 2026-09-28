#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define SENSOR_PERIOD_MS          2000
#define SENSOR_TASK_PRIORITY      2
#define SENSOR_STACK_WORDS        384

#define DISPLAY_TASK_PRIORITY     1
#define DISPLAY_STACK_WORDS       512

#define INPUT_TASK_PRIORITY       3
#define INPUT_STACK_WORDS         256
#define INPUT_POLL_MS             50

#define ALARM_TASK_PRIORITY       2
#define ALARM_STACK_WORDS         384

#define MOTION_TASK_PRIORITY      3
#define MOTION_STACK_WORDS        256
#define MOTION_POLL_MS            100

#define INACTIVITY_TIMEOUT_MS     15000UL

#endif
