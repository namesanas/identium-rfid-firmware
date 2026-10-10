/*
 * loop_monitor.h
 *
 * Main-loop timing with the DWT cycle counter.
 *
 * The firmware is a cooperative super-loop, so its responsiveness is set
 * by the slowest task call. Each task call in main() is wrapped in
 * LOOP_MONITOR_RUN(); the monitor keeps the longest duration of every
 * task and of a whole loop pass. Read with GET_DIAG, cleared with
 * CLEAR_DIAG.
 */

#ifndef RFID_CONTROLLER_LOOP_MONITOR_H_
#define RFID_CONTROLLER_LOOP_MONITOR_H_

#include <stdint.h>

typedef enum
{
    LOOP_TASK_ETHERNET = 0,
    LOOP_TASK_CONFIG_PORT,
    LOOP_TASK_PROTOCOL,
    LOOP_TASK_GPIO,
    LOOP_TASK_RFID,
    LOOP_TASK_HOST,
    LOOP_TASK_COUNT
} LoopTask_t;

/* Starts the cycle counter. Call once, just before the main loop. */
void Loop_Monitor_Init(void);

uint32_t Loop_Monitor_Now(void);

void Loop_Monitor_Record(LoopTask_t task, uint32_t startCycles);

/* Call once at the end of every loop pass. */
void Loop_Monitor_PassEnd(void);

uint32_t Loop_Monitor_GetTaskMaxUs(LoopTask_t task);
uint32_t Loop_Monitor_GetPassMaxUs(void);
uint32_t Loop_Monitor_GetPassCount(void);

/* Short task name for reports, e.g. "ETH". */
const char *Loop_Monitor_GetTaskName(LoopTask_t task);

void Loop_Monitor_Clear(void);

#define LOOP_MONITOR_RUN(task, call)                    \
    do                                                  \
    {                                                   \
        uint32_t loopMonitorStart_ = Loop_Monitor_Now(); \
        call;                                           \
        Loop_Monitor_Record((task), loopMonitorStart_); \
    } while(0)

#endif /* RFID_CONTROLLER_LOOP_MONITOR_H_ */
