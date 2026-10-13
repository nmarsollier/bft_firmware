/* Motors - motors_enter_error_state.c
 *
 * Purpose: single entry point to put the motors subsystem into the
 * unrecoverable ERROR state, and track the fault cause so a power-loss
 * recovery cannot silently clear a hardware fault.
 *
 * Any in-flight RMT transmission is stopped by motors_rmt_reset_both() when
 * the motion loop exits; this function only flags the state.
 *
 * Once in ERROR, the motors module rejects all movement commands.
 * A hardware fault is latched and only a full reboot clears it; a power-loss
 * fault is recoverable when the 12 V rail returns.
 */
#include "motors_internal.h"

/* Latched hardware fault — set on GPIO/RMT init failure, never cleared. */
static bool s_hardware_fault = false;

void motors_enter_error_state(void) {
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.status = MOTORS_STATUS_ERROR;
    motors_state.guiding = false;
    portEXIT_CRITICAL(&motors_state_lock);
}

void motors_enter_hardware_fault(void) {
    s_hardware_fault = true;
    motors_enter_error_state();
}

bool motors_has_hardware_fault(void) {
    return s_hardware_fault;
}
