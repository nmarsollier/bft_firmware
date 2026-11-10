/* limits_harness.c — small host harness for motors_limits.c.
 *
 * Compiled only by test_wait_idle.py, alongside phd_sim.c which provides the
 * FreeRTOS/RMT/GPIO stubs and the motors_state global. */

#include "motors/motors.h"

/* Return 1 when motors_set_current_as_home() confirmed idle and applied the
 * change, 0 when it refused (timed out). */
int sim_set_home(void) { return motors_set_current_as_home() ? 1 : 0; }
