#ifndef SIM_PICO_STDLIB_H_
#define SIM_PICO_STDLIB_H_
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
static inline uint32_t to_ms_since_boot(uint64_t t) { return (uint32_t)(t / 1000); }
uint64_t get_absolute_time(void);
static inline void gpio_init(uint32_t p) { (void)p; }
static inline void gpio_set_dir(uint32_t p, bool out) { (void)p; (void)out; }
static inline void gpio_put(uint32_t p, bool v) { (void)p; (void)v; }
static inline bool gpio_get(uint32_t p) { (void)p; return false; }
static inline void gpio_pull_up(uint32_t p) { (void)p; }
#define GPIO_OUT true
#define GPIO_IN  false
#ifdef __cplusplus
}
#endif
#endif
