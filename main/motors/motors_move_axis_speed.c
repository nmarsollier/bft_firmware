/* Motors - motors_move_axis_speed.c
 *
 * Purpose: request continuous single-axis speed motion.
 * Positive rate = forward, negative = reverse, zero = stop that axis.
 * Used by MoveAxis, joystick, and guiding.
 *
 * Speeds are clamped to [-MOTORS_MAX_SLEW_SPEED_DPS, +MOTORS_MAX_SLEW_SPEED_DPS].
 */
#include "motors.h"
#include "motors_internal.h"

MotorResultCode motors_set_move_axis_speed(float ra_speed, float dec_speed) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    if (ra_speed == 0.0f && dec_speed == 0.0f) {
        return motors_stop();
    }

    if (motors_status_is_parked(motors_state.status)) {
        return MOTOR_ERR_PARKED;
    }

    /* Clamp to hardware-safe maximum. */
    float max_dps = MOTORS_MAX_SLEW_SPEED_DPS;
    if (ra_speed > max_dps) ra_speed = max_dps;
    if (ra_speed < -max_dps) ra_speed = -max_dps;
    if (dec_speed > max_dps) dec_speed = max_dps;
    if (dec_speed < -max_dps) dec_speed = -max_dps;

    MotionCommand cmd = {
        .type = MOTION_CMD_MOVE_AXIS,
        .ra_target_deg = 0.0f, /* set by the task from limits */
        .dec_target_deg = 0.0f,
        .ra_speed = ra_speed,
        .dec_speed = dec_speed,
        .tracking_mode = TRACKING_NONE,
    };
    if (!motors_queue_put(&cmd)) {
        return MOTOR_ERR_BUSY;
    }
    return MOTOR_OK;
}
