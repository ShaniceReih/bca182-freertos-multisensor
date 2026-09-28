# Part XVII — Deliberate FreeRTOS Fault Experiments

This section documents three deliberate FreeRTOS fault experiments performed in Wokwi. Each fault was introduced temporarily, observed, analyzed, and then removed so that the final implementation remained correct.

---

## Fault Experiment 1 — Remove Blocking From a Continuously Executing Task

### Temporary Change

The normal `vTaskDelay()` at the end of `InputTask` was temporarily disabled.

Normally, `InputTask` performs a small amount of work and then blocks for the configured polling interval. Removing this delay caused the task to execute continuously while the system was ACTIVE.

### Observed Behavior

After the scheduler started, `InputTask` repeatedly produced:

```text
InputTask -> encoder button pressed
```

The message appeared continuously and flooded the Serial Monitor.

During the captured test period, the normal periodic output from `SensorTask`, `AlarmTask`, and `DisplayTask` was no longer observed.

### Explanation

`InputTask` has a relatively high FreeRTOS priority. Without a blocking delay, it remained continuously in the Ready or Running state.

Lower-priority tasks were therefore at risk of starvation because the higher-priority task continuously remained ready to execute.

The processor was also kept busy continuously instead of allowing the task to enter the Blocked state between polling operations.

This demonstrates why continuously executing FreeRTOS tasks should normally block, delay, or wait for an event after completing a finite amount of work.

### Effect on the System

- Continuous execution of `InputTask`
- Serial output flooding
- Increased CPU usage
- Starvation risk for lower-priority tasks
- Reduced overall system responsiveness

### Restoration

The original `vTaskDelay()` was restored after the experiment.

---

## Fault Experiment 2 — Assign an Unnecessarily High Task Priority

### Temporary Change

The priority of `InputTask` was temporarily increased from:

```cpp
#define INPUT_TASK_PRIORITY 3
```

to:

```cpp
#define INPUT_TASK_PRIORITY 4
```

This temporarily made `InputTask` the highest-priority application task.

### Observed Behavior

The system continued operating normally during the observed test period.

`SensorTask`, `AlarmTask`, `DisplayTask`, and `MotionTask` continued producing regular output.

No obvious starvation or major loss of responsiveness was observed.

### Explanation

Although `InputTask` had the highest application priority, it still used `vTaskDelay()` after completing its polling work.

Therefore, it regularly entered the Blocked state and allowed lower-priority tasks to execute.

This experiment shows that a high priority does not automatically cause starvation. The greater risk occurs when a high-priority task performs excessive work or fails to block.

An unnecessarily high priority can still increase scheduling latency for lower-priority tasks and can become problematic if the task workload increases.

### Effect on the System

- No visible failure was observed
- `InputTask` received CPU time immediately when Ready
- Lower-priority tasks continued executing because `InputTask` still blocked
- Potential scheduling latency remained

### Restoration

`InputTask` priority was restored from priority 4 to its intended priority 3.

---

## Fault Experiment 3 — Remove Serial Mutex Protection

### Temporary Change

The recursive mutex protection around `Log()` was temporarily removed.

During the experiment, `Log()` directly called `HAL_UART_Transmit()` without first obtaining `serialMutex`.

### Observed Behavior

No obvious corrupted or interleaved Serial output was observed during the test period.

Messages from the different FreeRTOS tasks still appeared as complete readable lines.

The system also continued performing its normal operations, including sensor updates and the ACTIVE-to-INACTIVE state transition.

### Explanation

The absence of visible corruption during this test does not mean that unprotected UART access is safe.

UART is a shared resource used by several tasks. Without mutex protection, multiple tasks may attempt to access the UART at approximately the same time.

In this particular Wokwi run, the scheduling and transmission timing did not produce a visible collision.

Under different timing conditions or higher logging activity, unprotected UART access could cause:

- interleaved messages
- corrupted output
- race conditions
- unpredictable UART behavior

The mutex therefore remains necessary to guarantee serialized access to the shared UART resource.

### Restoration

The original mutex-protected implementation of `Log()` was restored after the experiment.

---

## Overall Findings

The three experiments demonstrated important FreeRTOS scheduling and synchronization principles.

Removing blocking from a high-priority task produced the most severe observed effect because the task remained continuously Ready and created a starvation risk for lower-priority tasks.

Increasing task priority alone did not produce a visible failure because the task still blocked regularly. This demonstrates that task priority must be considered together with task execution time and blocking behavior.

Removing UART mutex protection did not produce visible corruption during the observed run, but it removed the synchronization guarantee for a shared hardware resource. The mutex is therefore still required in the final design.

All deliberate faults were removed after testing, and the correct project implementation was restored.