#ifndef SIM_TASK_H_
#define SIM_TASK_H_
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING     1
#define taskSCHEDULER_SUSPENDED   2
#ifdef __cplusplus
extern "C" {
#endif
// Real threads, so background firmware tasks (e.g. the display render task)
// actually run instead of being silently dropped.
BaseType_t xTaskCreate(void (*fn)(void*), const char *name, uint16_t stack,
                       void *arg, UBaseType_t prio, TaskHandle_t *out);
void vTaskDelete(TaskHandle_t t);
void vTaskSuspend(TaskHandle_t t);
void vTaskResume(TaskHandle_t t);
BaseType_t xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskPriorityGet(TaskHandle_t t);
void vTaskPrioritySet(TaskHandle_t t, UBaseType_t p);
const char *pcTaskGetName(TaskHandle_t t);
#ifdef __cplusplus
}
#endif
#endif
