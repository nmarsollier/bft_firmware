/* Motors - motors_ramp.c
 *
 * Purpose: slew acceleration profile and duration estimate.
 *
 * The distance-based trapezoidal velocity ramp: velocity ramps linearly with
 * travelled distance from MIN_SLEW_CDS up to the (capped) target and back down.
 * Pure functions — no mutable state — so the same inputs always produce the
 * same output.  motors_slew_duration_estimate() is the closed-form integral
 * used by the mount layer to compensate a target for the sky's drift over the
 * actual travel time.
 */
#include "motors_internal.h"

#include <math.h>

/* Minimum slew velocity in centidegrees/second — floor for ramp curves. */
#define MIN_SLEW_CDS 80

/* Distance threshold in centidegrees for the constant-slow-speed regime. */
#define SHORT_SLEW_CDS   200   /* constant slow speed below this */

/* Accel/decel ramp window in centidegrees — fixed angular distance
 * regardless of total travel (5 degrees). */
#define ACCEL_DIST_CDS   1000
#define DECEL_DIST_CDS   1000

/*
 * Velocity percentage (0-100) at a given travelled distance for the
 * distance-based trapezoidal profile: the first ACCEL_DIST_CDS centidegrees
 * are linear acceleration, the last DECEL_DIST_CDS are linear deceleration,
 * and the middle is cruise.  Moves shorter than the full accel+decel window
 * use a triangular profile (accel first half, decel second half).
 */
static int ramp_percent(int travelled_cds, int distance_cds) {
    const int ramp_total = ACCEL_DIST_CDS + DECEL_DIST_CDS;

    if (distance_cds <= ramp_total) {
        int half = distance_cds / 2;
        if (half <= 0)
            return 100;
        if (travelled_cds < half)
            return travelled_cds * 100 / half;
        return (distance_cds - travelled_cds) * 100 / half;
    }

    if (travelled_cds < ACCEL_DIST_CDS)
        return travelled_cds * 100 / ACCEL_DIST_CDS;
    if (travelled_cds < distance_cds - DECEL_DIST_CDS)
        return 100;
    return (distance_cds - travelled_cds) * 100 / DECEL_DIST_CDS;
}

/*
 * Compute the effective velocity for a single axis during a slew.
 *
 * target_vel_cds — target velocity in centidegrees/second (user-specified).
 * travelled_steps — steps taken so far in this slew.
 * distance_steps — total step count for this slew.
 * distance_cds   — total angular distance in centidegrees (precomputed).
 *
 * Returns velocity in deg/s (float).
 */
float ramp_velocity(int target_vel_cds, int64_t travelled_steps,
                    int64_t distance_steps, int distance_cds) {
    if (target_vel_cds == 0)
        return 0.0f;

    if (distance_cds == 0)
        return (float) target_vel_cds / 100.0f;

    if (distance_cds < SHORT_SLEW_CDS)
        return (float) MIN_SLEW_CDS / 100.0f;

    int capped_vel = target_vel_cds;
    if (distance_cds < (int)(MOTORS_SLEW_GENTLE_DEG * 100.0f)) {
        int speed_limit = MIN_SLEW_CDS * 4;
        if (capped_vel > speed_limit) capped_vel = speed_limit;
    }

    if (travelled_steps < 0) travelled_steps = -travelled_steps;
    /* Travelled distance in centidegrees, proportional to the step count.
     * Slew distance is bounded (< ~4M microsteps at 64 µsteps), so the
     * 64-bit product cannot overflow. */
    int travelled_cds = (int) ((int64_t) travelled_steps * (int64_t) distance_cds
                               / distance_steps);

    int vel = MIN_SLEW_CDS + (capped_vel - MIN_SLEW_CDS) * ramp_percent(travelled_cds, distance_cds) / 100;
    return (float) vel / 100.0f;
}

/*
 * Estimated single-axis slew duration for a distance and requested speed.
 *
 * Velocity ramps linearly with *distance* from MIN_SLEW_CDS up to the (capped)
 * target and back down — a trapezoid, or a triangle when the move is shorter
 * than the accel+decel window, or a constant MIN_SLEW_CDS below SHORT_SLEW_CDS.
 * dt = ∫ dx / v(x) closed form, mirroring ramp_velocity() above.
 */
float motors_slew_duration_estimate(float distance_deg, float speed_dps) {
    if (distance_deg <= 0.0f || speed_dps <= 0.0f) {
        return 0.0f;
    }

    float min_dps = (float) MIN_SLEW_CDS / 100.0f;
    float short_deg = (float) SHORT_SLEW_CDS / 100.0f;
    float gentle_deg = MOTORS_SLEW_GENTLE_DEG;
    float accel_deg = (float) ACCEL_DIST_CDS / 100.0f;
    float decel_deg = (float) DECEL_DIST_CDS / 100.0f;

    if (distance_deg < short_deg) {
        return distance_deg / min_dps;
    }

    float capped = speed_dps;
    if (distance_deg < gentle_deg) {
        float limit = min_dps * 4.0f;
        if (capped > limit) capped = limit;
    }

    float ramp_ln = logf(capped / min_dps) / (capped - min_dps);

    if (distance_deg <= accel_deg + decel_deg) {
        /* Triangular profile — no cruise. */
        return distance_deg * ramp_ln;
    }

    float t_ramp = accel_deg * ramp_ln;
    return 2.0f * t_ramp + (distance_deg - accel_deg - decel_deg) / capped;
}
