/* Motors - motors_park.c
 *
 * Purpose: park both axes immediately.
 *
 * Stops the motion loop and updates state directly — no queue round-trip.
 */
#include "motors.h"
#include "motors_internal.h"

MotorResultCode motors_park(void) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    motors_queue_clear();
    motors_motion_stop();
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.status = MOTORS_STATUS_PARKED;
    motors_state.tracking = TRACKING_NONE;
    portEXIT_CRITICAL(&motors_state_lock);
    return MOTOR_OK;
}
