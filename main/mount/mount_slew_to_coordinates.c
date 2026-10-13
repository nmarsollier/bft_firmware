/* Mount - mount_slew_to_coordinates.c
 *
 * Purpose: move the mount to a requested equatorial position.
 */
#include "mount.h"
#include "mount_internal.h"

#include <math.h>
#include <time.h>

#include "motors.h"

MountResult mount_slew_to_coordinates(float ra, float dec, int speed_rate) {
    if (mount_is_error()) {
        return mount_result_error_state();
    }

    /* GOTO needs a valid clock: without it LST/HA are wrong and the mount
     * would point at the wrong sky position. */
    if (!mount_time_valid) {
        return mount_result_error("Time not synchronized — set the UTC date first");
    }

    EquatorialCoordinates eq = {
        .ra_hours = ra,
        .dec_deg = dec
    };

    AxisCoordinates current = { .ra_axis_deg = motors_get_ra_deg(),
                                .dec_axis_deg = motors_get_dec_deg() };

    /* The sky keeps rotating during the slew: converting the destination once
     * at the request time would point at where the target *was*.  Convert at
     * the predicted arrival time instead — a first-order compensation for the
     * hour angle that advances during the move (≈ 15 arcsec/s). */
    time_t now = time(NULL);

    AxisCoordinates axis_now;
    if (!equatorial_to_axis(eq, current, now, &axis_now)) {
        return mount_result_error("Target unreachable — no valid axis solution");
    }

    float speed = motors_get_slewing_speed(speed_rate);
    float ra_dist  = fabsf(axis_now.ra_axis_deg  - current.ra_axis_deg);
    float dec_dist = fabsf(axis_now.dec_axis_deg - current.dec_axis_deg);
    float max_dist = (ra_dist > dec_dist) ? ra_dist : dec_dist;
    /* Base move time plus ~2 s for the accel/decel ramp and queue latency. */
    float duration_s = (speed > 0.0f) ? (max_dist / speed) + 2.0f : 0.0f;
    time_t arrive_at = now + (time_t) duration_s;

    AxisCoordinates axis;
    if (!equatorial_to_axis(eq, current, arrive_at, &axis)) {
        return mount_result_error("Target unreachable — no valid axis solution");
    }

    MotorResultCode rc1 = motors_slew_to_angle(axis.ra_axis_deg, axis.dec_axis_deg, speed_rate);

    if (rc1 != MOTOR_OK) {
        return mount_result_error("Failed to start slew to coordinates");
    }

    return mount_result_ok();
}
