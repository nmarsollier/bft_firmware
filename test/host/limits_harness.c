/* limits_harness.c — small host harness for motors_limits.c.
 *
 * Compiled by test_wait_idle.py and test_limits.py, alongside phd_sim.c which
 * provides the FreeRTOS/RMT/GPIO stubs and the motors_state global. */

#include "motors/motors.h"
#include "motors/motors_internal.h"

/* Return 1 when motors_set_current_as_home() confirmed idle and applied the
 * change, 0 when it refused (timed out). */
int sim_set_home(void) { return motors_set_current_as_home() ? 1 : 0; }

/* Mark the motion task idle so the limit/home setters' wait_idle succeeds. */
void sim_mark_idle(void) { motors_motion_idle_give(); }

/* Axis-limit setters — return 1 applied, 0 refused (busy/timeout). */
int sim_set_ra_left_limit(void)   { return motors_set_current_as_ra_left_limit()   ? 1 : 0; }
int sim_set_ra_right_limit(void)  { return motors_set_current_as_ra_right_limit()  ? 1 : 0; }
int sim_set_dec_left_limit(void)  { return motors_set_current_as_dec_left_limit()  ? 1 : 0; }
int sim_set_dec_right_limit(void) { return motors_set_current_as_dec_right_limit() ? 1 : 0; }

/* Current logical limits (post-clamp) — read back for the assertions. */
float sim_ra_min(void)  { return motors_state.limits.ra_min; }
float sim_ra_max(void)  { return motors_state.limits.ra_max; }
float sim_dec_min(void) { return motors_state.limits.dec_min; }
float sim_dec_max(void) { return motors_state.limits.dec_max; }
