// Host FreeRTOS shim - REAL TIME build.
//
// Unlike the earlier batch harness, the simulator now runs several real
// threads concurrently (plant ticker, charge state machine, HTTP server), so
// these map onto real OS primitives rather than no-ops. Delays are genuine
// wall-clock delays: a 20 second throw takes 20 seconds, which is what makes
// the web GUI behave like the real device.
#ifndef SIM_FREERTOS_H_
#define SIM_FREERTOS_H_
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef void * TaskHandle_t;
typedef void * QueueHandle_t;
typedef void * SemaphoreHandle_t;

#define portTICK_PERIOD_MS   1u
#define portMAX_DELAY        0xFFFFFFFFu
#define pdTRUE               1
#define pdFALSE              0
#define pdPASS               1
#define pdFAIL               0
#define pdMS_TO_TICKS(ms)    ((TickType_t)(ms))
#define configMINIMAL_STACK_SIZE 256
#define tskIDLE_PRIORITY     0

#ifdef __cplusplus
extern "C" {
#endif

// Milliseconds since the simulator started (real wall clock).
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *previous, TickType_t increment);

// Kept for source compatibility with the batch harness.
TickType_t sim_now_ticks(void);
void sim_advance_ticks(TickType_t ticks);

#ifdef __cplusplus
}
#endif
#endif
