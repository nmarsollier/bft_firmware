/* Motors - motors_start_tracking.c
 *
 * Purpose: start continuous tracking for the selected mode.
 *
 * Status and tracking mode are set by process_command() when the
 * motion task dequeues the TRACK command — no soft gate needed.
 */
#include "motors.h"
#include "motors_internal.h"

MotorResultCode motors_start_tracking(TrackingMode mode) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    if (mode == TRACKING_NONE) {
        return motors_stop();
    }

    if (motors_status_is_parked(motors_state.status)) {
        return MOTOR_ERR_PARKED;
    }

    float ra_speed = motors_get_tracking_speed(mode);

    MotionCommand cmd = {
        .type = MOTION_CMD_TRACK,
        .ra_target_deg = 0.0f, /* set by the task from limits */
        .dec_target_deg = 0.0f,
        .ra_speed = ra_speed,
        .dec_speed = 0.0f,
        .tracking_mode = mode,
    };
    if (!motors_queue_put(&cmd)) {
        return MOTOR_ERR_BUSY;
    }

    return MOTOR_OK;
}
