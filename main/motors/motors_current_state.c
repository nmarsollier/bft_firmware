/* Motors - motors_current_state.c
 *
 * Purpose: return a copy of the authoritative motors state,
 * and provide degree-format accessors for external consumers.
 */
#include "motors.h"
#include "motors_internal.h"

/*
 * Return a snapshot copy of the motors module's authoritative state.
 * External consumers should use this for status and telemetry reads.
 *
 * The whole copy is taken under motors_state_lock so a reader on another core
 * cannot observe a torn int64_t counter or a half-updated multi-field state
 * transition.
 */
MotorsState motors_current_state(void) {
    MotorsState copy;
    portENTER_CRITICAL(&motors_state_lock);
    copy = motors_state;
    portEXIT_CRITICAL(&motors_state_lock);
    return copy;
}

float motors_steps_to_deg(int64_t steps) {
    return (float)steps * motors_get_deg_per_microstep();
}

float motors_get_ra_deg(void) {
    int64_t steps;
    portENTER_CRITICAL(&motors_state_lock);
    steps = motors_state.ra_steps;
    portEXIT_CRITICAL(&motors_state_lock);
    return motors_steps_to_deg(steps);
}

float motors_get_dec_deg(void) {
    int64_t steps;
    portENTER_CRITICAL(&motors_state_lock);
    steps = motors_state.dec_steps;
    portEXIT_CRITICAL(&motors_state_lock);
    return motors_steps_to_deg(steps);
}
