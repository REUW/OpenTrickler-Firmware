#ifndef SIM_QUEUE_H_
#define SIM_QUEUE_H_
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t sz) { (void)len; (void)sz; return (QueueHandle_t)1; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t t) { (void)q; (void)item; (void)t; return pdTRUE; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t t) { (void)q; (void)buf; (void)t; return pdFALSE; }
static inline BaseType_t xQueueReset(QueueHandle_t q) { (void)q; return pdTRUE; }
#ifdef __cplusplus
}
#endif
#endif
