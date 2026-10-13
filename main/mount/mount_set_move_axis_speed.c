#include "mount.h"
#include "mount_internal.h"
#include "motors.h"

#include <math.h>

static TrackingMode s_saved_tracking = TRACKING_NONE;

/* Per-axis MoveAxis rate cache (deg/s).  Lives in the mount layer so any
 * STOP — which calls mount_move_axis_reset() — clears it, preventing a stale
 * residual rate from a previous manual move leaking into the next MoveAxis. */
static float s_ra_rate = 0.0f;
static float s_dec_rate = 0.0f;

MountResult mount_set_move_axis_speed(float ra_speed, float dec_speed) {
    if (mount_is_error()) {
        return mount_result_error_state();
    }

    if (ra_speed == 0.0f && dec_speed == 0.0f) {
        /* Read before mount_stop() — mount_move_axis_reset() clears it. */
        TrackingMode to_restore = s_saved_tracking;
        MountResult r = mount_stop();
        if (r.ok && to_restore != TRACKING_NONE) {
            /* A failed stop must not be reported as a clean manual stop, nor
             * must tracking be restored on top of it. */
            r = mount_set_tracking(to_restore);
        }
        return r;
    }

    MotorsState s = motors_current_state();
    if (s.status == MOTORS_STATUS_TRACKING && s.tracking != TRACKING_NONE) {
        /* Stop tracking first.  mount_stop() calls mount_move_axis_reset(),
         * which wipes both the saved tracking mode and the MoveAxis rate
         * cache — remember the tracking mode so a later full stop restores it.
         * If the stop fails, abort: the manual move must not start on a mount
         * still tracking, nor leave the saved mode half-remembered. */
        TrackingMode saved = s.tracking;
        MountResult stop_result = mount_stop();
        if (!stop_result.ok) {
            return stop_result;
        }
        s_saved_tracking = saved;
    }

    MotorResultCode rc = motors_set_move_axis_speed(ra_speed, dec_speed);
    if (rc == MOTOR_OK) {
        /* Commit the cache only once motors accepted the order.  A rejected
         * order (parked, queue full) must not leave a residual rate that a
         * later single-axis change would reapply to the untouched axis. */
        s_ra_rate = ra_speed;
        s_dec_rate = dec_speed;
    }
    return motors_result_code_error_result(rc);
}

/*
 * Move a single axis (0 = RA, 1 = DEC) continuously at `rate` deg/s,
 * preserving the other axis's current rate.  Clamps to the safe slew ceiling.
 * The candidate rates are built from the accepted cache and committed only if
 * motors accepts them (inside mount_set_move_axis_speed).
 */
MountResult mount_set_move_axis_rate(int axis, float rate) {
    float hi = motors_get_slewing_speed(4);
    float clamped = fmaxf(fminf(rate, hi), -hi);

    float next_ra = s_ra_rate;
    float next_dec = s_dec_rate;

    if (axis == 0) {
        next_ra = clamped;
    } else if (axis == 1) {
        next_dec = clamped;
    } else {
        return mount_result_error("Axis not supported");
    }

    return mount_set_move_axis_speed(next_ra, next_dec);
}

void mount_move_axis_reset(void) {
    s_saved_tracking = TRACKING_NONE;
    s_ra_rate = 0.0f;
    s_dec_rate = 0.0f;
}
