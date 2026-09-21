#ifndef SIM_HARNESS_H_
#define SIM_HARNESS_H_

#include <stdint.h>
#include <stdbool.h>
#include "motors.h"
#include "scale.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called by the motor stub on every commanded speed change. Used for the
// motor-trace debug output and for measuring phase durations.
void sim_harness_on_motor_command(motor_select_t motor, float rps);

// Verbosity: 0 = summary only, 1 = per-charge lines, 2 = full motor trace.
extern int g_sim_verbosity;

#ifdef __cplusplus
}
#endif

#endif  // SIM_HARNESS_H_
