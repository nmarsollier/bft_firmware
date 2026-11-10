/* Motors - motors_slew_to_angle.c
 *
 * Purpose: move both axes to absolute axis angles (degrees).
 *
 * Pauses and resumes tracking automatically.  Target validation
 * happens here; the motion task only executes the commanded move.
 */
#include "motors.h"
#include "motors_internal.h"

#include "esp_log.h"

static const char *TAG = "MOTORS_SLEW_TO_ANGLE";

/*
 * Move both axes to absolute axis angles in degrees.
 *
 * Validates both targets against axis limits before enqueuing the SLEW
 * command.  If tracking is active it is paused for the slew and
 * automatically resumed afterwards — the caller does not need to
 * manage tracking state.
 *
 * Parameters:
 *   ra_deg   — target RA axis angle in degrees (validated against limits)
 *   dec_deg  — target DEC axis angle in degrees
 *   speed_rate — slew profile (1=0.5°/s, 2=1°/s, 3=2.5°/s, default=4°/s)
 */
MotorResultCode motors_slew_to_angle(float ra_deg, float dec_deg, int speed_rate) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    if (motors_status_is_parked(motors_state.status)) {
        return MOTOR_ERR_PARKED;
    }

    /* Validate targets BEFORE pausing tracking: a rejected slew must not leave
     * tracking paused. */
    if (!motors_is_valid_ra(ra_deg)) {
        ESP_LOGW(TAG, "Rejected slew: RA out of range (%.3f)", ra_deg);
        return MOTOR_ERR_OUT_OF_RANGE;
    }

    if (!motors_is_valid_dec(dec_deg)) {
        ESP_LOGW(TAG, "Rejected slew: DEC out of range (%.3f)", dec_deg);
        return MOTOR_ERR_OUT_OF_RANGE;
    }

    TrackingMode currTracking = TRACKING_NONE;
    if (motors_state.status == MOTORS_STATUS_TRACKING
        && motors_state.tracking != TRACKING_NONE) {
        currTracking = motors_state.tracking;
        MotorResultCode stop_rc = motors_stop();
        if (stop_rc != MOTOR_OK) {
            return stop_rc;
        }
    }

    float speed = motors_get_slewing_speed(speed_rate);

    MotionCommand cmd = {
        .type = MOTION_CMD_SLEW,
        .ra_target_deg = ra_deg,
        .dec_target_deg = dec_deg,
        .ra_speed = speed,
        .dec_speed = speed,
        .resume_tracking = currTracking,
    };
    if (!motors_queue_put(&cmd)) {
        /* Slew + tracking resume are one atomic operation.  On enqueue failure
         * restore the tracking that was just paused (the queue was cleared by
         * motors_stop, so this re-enqueue succeeds). */
        if (currTracking != TRACKING_NONE) {
            motors_start_tracking(currTracking);
        }
        return MOTOR_ERR_BUSY;
    }

    return MOTOR_OK;
}
