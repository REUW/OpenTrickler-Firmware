#ifndef SIM_SEMPHR_H_
#define SIM_SEMPHR_H_
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
// Real mutexes: the HTTP thread and the charge thread genuinely contend for
// the config and AI tuning structures, so these must actually lock.
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s);
void vSemaphoreDelete(SemaphoreHandle_t s);
#ifdef __cplusplus
}
#endif
#endif
