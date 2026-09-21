#ifndef SIM_PICO_TIME_H_
#define SIM_PICO_TIME_H_
#include "pico/stdlib.h"
typedef uint64_t absolute_time_t;
uint64_t time_us_64(void);
uint32_t time_us_32(void);
static inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return (int64_t)b - (int64_t)a; }
#endif
