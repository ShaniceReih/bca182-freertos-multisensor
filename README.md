# BCA182 FreeRTOS Multisensor Room Monitoring System

## Project Overview

This project is my implementation of a real-time room monitoring system using an **STM32F103C8T6 Blue Pill** and **FreeRTOS**.

The system monitors temperature, humidity, ambient light, and motion. The sensor readings are displayed on an SSD1306 OLED, while a rotary encoder is used to move between the different display pages.

A buzzer is used for the temperature alarm. The system also has ACTIVE and INACTIVE states. If no motion is detected for 15 seconds, the OLED is blanked and the system becomes INACTIVE. When the PIR sensor detects motion again, the system returns to ACTIVE.

The project was developed and tested using PlatformIO, STM32Cube HAL, FreeRTOS, Wokwi, Unity, Cppcheck, Git, and GitHub.

---

## Features

- Temperature and humidity monitoring using DHT22
- Ambient light monitoring using an LDR
- SSD1306 OLED display
- Four display pages
- Rotary encoder navigation
- High- and low-temperature alarm
- PWM buzzer output
- PIR motion detection
- ACTIVE and INACTIVE system states
- Automatic OLED blanking after inactivity
- Five FreeRTOS tasks
- Queue-based communication
- Task notifications
- Recursive mutex for UART logging
- Native unit testing
- Static code analysis
- Functional verification in Wokwi
- FreeRTOS fault experiments

---

## Learning Objectives

This laboratory helped me understand how an embedded application can be divided into separate real-time tasks instead of placing everything inside one main loop.

Some of the concepts I applied were:

- FreeRTOS task creation
- task priorities
- periodic execution
- task blocking
- `vTaskDelay()`
- `vTaskDelayUntil()`
- queues
- mutexes
- task notifications
- interrupt-based input
- state-machine design
- modular C++ programming
- unit testing
- static analysis
- Git version control

---

## System Architecture

The system is divided into input devices, FreeRTOS tasks, communication objects, and outputs.

```text
 DHT22                LDR
   |                   |
   +-------+-----------+
           |
      SensorTask
        /     \
       /       \
      v         v
 Display Queue  Alarm Queue
      |             |
      v             v
 DisplayTask     AlarmTask ------> Buzzer
      |
      v
     OLED


 Rotary Encoder ----> InputTask ----> Display Mode Queue
                                      |
                                      v
                                  DisplayTask


 PIR Sensor -------> MotionTask
                         |
                         v
                  ACTIVE / INACTIVE
                         |
                  Task Notification
                         |
                         v
                    DisplayTask
```

**Figure 1 — System architecture.**  
This diagram shows the main flow of data from the sensors and user inputs to the FreeRTOS tasks and system outputs.

---

## FreeRTOS Architecture

The firmware uses five FreeRTOS tasks. Each task has one main responsibility.

## Task Design

| Task | Priority | Responsibility | Blocking / Timing |
|---|---:|---|---|
| `SensorTask` | 2 | Reads temperature, humidity, light, and motion status | `vTaskDelayUntil()` |
| `DisplayTask` | 1 | Updates the OLED and shows the selected page | Waits for data and notifications |
| `InputTask` | 3 | Reads the rotary encoder | `vTaskDelay()` |
| `AlarmTask` | 2 | Checks temperature and controls the buzzer | Waits for sensor data |
| `MotionTask` | 3 | Reads the PIR sensor and updates the system state | `vTaskDelayUntil()` |

### Priority Design

`InputTask` and `MotionTask` use priority 3 because user input and motion detection should respond quickly.

`SensorTask` and `AlarmTask` use priority 2 because they are important but do not require the same immediate response as input events.

`DisplayTask` uses priority 1 because updating the OLED is less time-critical.

The higher-priority tasks still block or delay regularly. This is important because a high-priority task that never blocks can prevent lower-priority tasks from running.

---

## Inter-Task Communication

The tasks communicate using three queues, task notifications, and one UART mutex.

```text
                 +----------------+
                 |   SensorTask   |
                 +----------------+
                    |          |
                    |          |
                    v          v
          displaySensorQueue  alarmSensorQueue
                    |          |
                    v          v
              DisplayTask   AlarmTask


 Rotary Encoder
       |
       v
   InputTask
       |
       v
 displayModeQueue
       |
       v
  DisplayTask


 MotionTask --------+
                    |
 AlarmTask ---------+----> Task Notifications ----> DisplayTask


 SensorTask   \
 DisplayTask   \
 InputTask      >---- serialMutex ----> UART1
 AlarmTask     /
 MotionTask   /
```

**Figure 2 — FreeRTOS task communication.**  
Queues are used when actual data must be transferred. Task notifications are used for simple events, while the mutex protects the shared UART peripheral.

### Sensor Queues

`SensorTask` produces sensor information using:

```cpp
struct SensorData
{
    float temperature;
    float humidity;
    int lightLevel;
    bool motionDetected;
};
```

The data is sent through:

```text
displaySensorQueue
alarmSensorQueue
```

Both are single-element queues. `xQueueOverwrite()` is used because the display and alarm only need the latest sensor reading.

### Display Mode Queue

`InputTask` sends the selected OLED page through:

```text
displayModeQueue
```

The display pages are:

```text
TEMPERATURE
HUMIDITY
LIGHT
MOTION
```

Clockwise encoder rotation moves to the next page. Counterclockwise rotation moves to the previous page.

### Task Notifications

Task notifications are used for events such as:

```text
ACTIVE
INACTIVE
MOTION
ALARM
```

They provide a lightweight way for `MotionTask` and `AlarmTask` to inform `DisplayTask` about important changes.

### UART Mutex

Several tasks print messages through UART1.

The shared UART is protected using:

```text
serialMutex
```

This prevents multiple tasks from using the UART at the same time.

---

## Hardware / Simulated Components

| Component | Purpose |
|---|---|
| STM32F103C8T6 Blue Pill | Main microcontroller |
| DHT22 | Temperature and humidity |
| LDR | Ambient light |
| SSD1306 OLED | Display |
| Rotary Encoder | User input |
| PIR Sensor | Motion detection |
| Buzzer | Temperature alarm |
| UART1 | Debug output |

---

## Pin Configuration

| Device | STM32 Pin |
|---|---|
| DHT22 Data | PB0 |
| LDR ADC | PA0 |
| OLED SCL | PB6 |
| OLED SDA | PB7 |
| Encoder CLK | PA1 |
| Encoder DT | PA2 |
| Encoder SW | PA3 |
| Buzzer PWM | PA8 |
| PIR OUT | PB1 |
| UART1 TX | PA9 |
| UART1 RX | PA10 |

The SSD1306 OLED uses I2C1 with address:

```text
0x3C
```

---

## State Machine

The room monitor has two states: ACTIVE and INACTIVE.

```text
                  +----------------+
                  |     ACTIVE     |
                  |                |
                  | OLED visible   |
                  | Input enabled  |
                  +-------+--------+
                          |
                          |
                 15 seconds without
                       motion
                          |
                          v
                  +----------------+
                  |    INACTIVE    |
                  |                |
                  | OLED blanked   |
                  | Input ignored  |
                  +-------+--------+
                          |
                          |
                    PIR motion
                      detected
                          |
                          +-----------> ACTIVE
```

**Figure 3 — System state machine.**  
The system starts in ACTIVE mode. After 15 seconds without motion it becomes INACTIVE. PIR motion returns it to ACTIVE.

### ACTIVE State

While ACTIVE:

- sensor monitoring continues
- the OLED is visible
- encoder navigation works
- alarm monitoring remains active

### INACTIVE State

While INACTIVE:

- the OLED is blanked
- encoder navigation is ignored
- sensor monitoring continues
- alarm monitoring continues
- PIR motion can reactivate the system

---

## Temperature Alarm

The alarm uses these limits:

| Temperature | System Response |
|---|---|
| Below 18 °C | LOW temperature alarm |
| 18 °C to 30 °C | NORMAL |
| Above 30 °C | HIGH temperature alarm |

The values `18.0 °C` and `30.0 °C` are considered normal.

The buzzer is turned on when the temperature is outside the allowed range.

---

## Repository Structure

The project was separated into modules so that `main.cpp` mainly handles initialization, RTOS object creation, task creation, and scheduler startup.

```text
project/
|
+-- include/
|   +-- alarm.h
|   +-- alarm_task.h
|   +-- app_config.h
|   +-- app_types.h
|   +-- display.h
|   +-- display_logic.h
|   +-- dht22.h
|   +-- input.h
|   +-- logging.h
|   +-- motion.h
|   +-- rtos_objects.h
|   +-- sensors.h
|   +-- ssd1306.h
|   +-- system_state.h
|
+-- src/
|   +-- alarm.cpp
|   +-- alarm_task.cpp
|   +-- display.cpp
|   +-- display_logic.cpp
|   +-- dht22.cpp
|   +-- input.cpp
|   +-- logging.cpp
|   +-- main.cpp
|   +-- motion.cpp
|   +-- rtos_objects.cpp
|   +-- sensors.cpp
|   +-- ssd1306.cpp
|   +-- system_state.cpp
|
+-- test/
|   +-- test_logic/
|       +-- test_main.cpp
|
+-- docs/
|   +-- functional-verification.md
|   +-- fault-experiments.md
|   +-- images/
|       +-- wokwi-circuit.png
|       +-- finished-system.png
|
+-- lib/
|   +-- FreeRTOS/
|
+-- diagram.json
+-- platformio.ini
+-- wokwi.toml
+-- README.md
```

---

## Getting Started

### Requirements

The project requires:

- Visual Studio Code
- PlatformIO
- Git
- Wokwi Simulator extension

Open the project folder in Visual Studio Code after cloning or downloading the repository.

---

## Building the Project

Build the STM32 firmware using:

```powershell
pio run -e bluepill_f103c8
```

A successful build creates the firmware used by Wokwi.

---

## Running the Wokwi Simulation

Build the project first:

```powershell
pio run -e bluepill_f103c8
```

Then start the Wokwi Simulator from Visual Studio Code.

During the simulation, the following inputs can be tested:

- DHT22 temperature
- DHT22 humidity
- LDR illumination
- encoder clockwise rotation
- encoder counterclockwise rotation
- encoder push button
- PIR motion

The OLED, buzzer, and Serial Monitor can be used to observe the response of the system.

---

## Wokwi Circuit

![Wokwi Circuit](docs/images/wokwi-circuit.png)

**Figure 4 — Wokwi circuit.**  
The complete simulated circuit showing the STM32 Blue Pill connected to the DHT22, LDR, SSD1306 OLED, rotary encoder, PIR sensor, and buzzer.

---

## Unit Testing

Hardware-independent functions were separated from hardware code so they could be tested using PlatformIO's native environment.

A total of **13 unit tests** were created.

### Alarm Logic — 5 Tests

The alarm tests check:

- below 18 °C
- exactly 18 °C
- normal temperature
- exactly 30 °C
- above 30 °C

### Display Navigation — 4 Tests

The navigation tests check:

- next page
- forward wraparound
- previous page
- reverse wraparound

### System State — 4 Tests

The state tests check:

- ACTIVE before timeout
- INACTIVE exactly at timeout
- INACTIVE after timeout
- motion returning the system to ACTIVE

Tests are run using:

```powershell
pio test -e native
```

Result:

```text
13 test cases: 13 succeeded
```

---

## Static Code Analysis

Static analysis was run using:

```powershell
pio check
```

Results:

| Severity | Findings |
|---|---:|
| HIGH | 0 |
| MEDIUM | 0 |
| LOW | 102 |

Both PlatformIO environments passed.

Most LOW findings were style-related, such as C-style casts and functions that Cppcheck could not detect as being used indirectly through callbacks or interrupt handling.

I reviewed the findings but did not change stable hardware code only to remove low-severity style warnings.

---

## Functional Verification

The full system was tested in Wokwi.

| Test ID | Input / Stimulus | Actual Result | Status |
|---|---|---|---|
| FT-01 | Temperature set to 27.3 °C | OLED and Serial updated to 27.3 °C | PASS |
| FT-02 | Humidity set to 77.0% | OLED updated to 77.0% | PASS |
| FT-03 | LDR illumination changed | Relative light reading changed to 10% | PASS |
| FT-04 | Encoder CW from LIGHT | Page changed to MOTION | PASS |
| FT-05 | Encoder CCW from MOTION | Page changed to LIGHT | PASS |
| FT-06 | Temperature set to 35.8 °C | Alarm activated and buzzer turned on | PASS |
| FT-07 | Temperature returned to 23.9 °C | Alarm returned to NORMAL and buzzer stopped | PASS |
| FT-08 | PIR triggered while ACTIVE | Motion detected | PASS |
| FT-09 | No motion for 15 seconds | System became INACTIVE and OLED blanked | PASS |
| FT-10 | PIR triggered while INACTIVE | System returned to ACTIVE | PASS |

All 10 required functional tests passed.

More detailed test observations are available in:

```text
docs/functional-verification.md
```

---

## FreeRTOS Fault Experiments

Three temporary faults were introduced during testing.

### Experiment 1 — Removing Task Blocking

The delay at the end of `InputTask` was temporarily removed.

The task then ran continuously and repeatedly printed:

```text
InputTask -> encoder button pressed
```

The Serial Monitor became flooded and normal lower-priority task output was no longer observed during the test.

This showed the starvation risk caused by a high-priority task that never blocks.

### Experiment 2 — Increasing Task Priority

`InputTask` was temporarily changed from priority 3 to priority 4.

The system continued operating normally because `InputTask` still used `vTaskDelay()`.

This showed that priority alone does not necessarily cause starvation. Blocking behavior is also important.

### Experiment 3 — Removing the UART Mutex

The mutex protection in `Log()` was temporarily removed.

No obvious interleaved output was observed during the test, but UART access was no longer protected.

The mutex was restored because UART is still a shared resource and concurrent access could produce problems under different timing conditions.

More details are available in:

```text
docs/fault-experiments.md
```

---

## Engineering Decisions

### Why I Used `vTaskDelayUntil()`

I used `vTaskDelayUntil()` for periodic tasks because it keeps execution based on a regular schedule rather than delaying relative to when the previous task iteration finishes.

### Why I Used Separate Queues

`SensorTask` sends data to both `DisplayTask` and `AlarmTask`.

Using two queues prevents the two consumers from competing for the same queue item.

### Why I Used `xQueueOverwrite()`

Only the newest sensor reading is needed, so keeping old values in a long queue would not be useful.

### Why Only `DisplayTask` Uses the OLED

Giving the OLED one owner keeps display code in one place and avoids simultaneous access to the I2C display.

### Why I Used Task Notifications

Task notifications are lightweight and are suitable for events that do not need to carry a large data structure.

### Why I Used a UART Mutex

Several tasks use UART for debugging. The mutex ensures that the shared UART resource is accessed in a controlled way.

### Why I Separated Logic From Hardware

Functions such as alarm checking, display navigation, and state evaluation do not need direct access to STM32 hardware.

Separating them made it possible to test them using native unit tests.

---

## Limitations

Current limitations include:

- testing was mainly performed in Wokwi
- the LDR reading is a relative percentage, not calibrated lux
- temperature alarm thresholds are fixed
- the inactivity timeout is fixed at 15 seconds
- sensor history is not stored
- there is no Wi-Fi or Bluetooth communication
- heavy UART logging can affect timing
- physical hardware may behave differently because of noise, component tolerance, and wiring conditions

---

## Future Improvements

Possible future improvements are:

- test the project using a physical STM32 Blue Pill
- calibrate the LDR reading
- allow adjustable alarm thresholds
- allow adjustable inactivity timeout
- add sensor data logging
- add Wi-Fi or Bluetooth
- create a remote monitoring interface
- improve the OLED display layout
- save configuration settings in flash
- add watchdog monitoring
- add FreeRTOS runtime statistics

---

## Finished System

![Finished System](docs/images/finished-system.png)

**Figure 5 — Finished room-monitoring system.**  
The completed Wokwi simulation running in ACTIVE mode with live sensor data displayed on the OLED.

---

## References and Acknowledgments

This project was developed for **BCA182 Embedded Systems Laboratory Activity 1**.

References and tools used during the project include:

- FreeRTOS documentation
- STM32Cube HAL documentation
- PlatformIO documentation
- Wokwi documentation
- Unity Test Framework
- Cppcheck
- Git and GitHub

The laboratory instructions were used as the main guide for the system design, implementation, and verification.