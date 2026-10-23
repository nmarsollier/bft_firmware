/* Motors - motors_home.c
 *
 * Purpose: move the mount to the home position.
 *
 * Always stops any motion in progress before enqueuing the home SLEW.
 * Without the explicit stop, a SLEWING mount would ignore the new command
 * because the motion task does not poll the queue while executing a slew.
 */
#include "motors.h"
#include "motors_internal.h"

MotorResultCode motors_home(void) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    if (motors_status_is_parked(motors_state.status)) {
        return MOTOR_ERR_PARKED;
    }

    motors_stop();
    /* An explicit home re-establishes the positive DEC orientation.  Wait for
     * the motion task to actually leave any in-flight MoveAxis so the reset is
     * not overwritten by a final orientation update from the aborted move. */
    motors_motion_wait_idle();
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.dec_orientation_negative = false;
    portEXIT_CRITICAL(&motors_state_lock);
    return motors_slew_to_angle(0.0f, 0.0f, 0);
}
