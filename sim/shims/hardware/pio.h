#ifndef SIM_HW_PIO_H_
#define SIM_HW_PIO_H_
#include <stdint.h>
#include <stdbool.h>
typedef struct { int dummy; } *PIO;
typedef struct { int dummy; } pio_sm_config;
#define pio0 ((PIO)0)
#define pio1 ((PIO)0)
#endif
