/* Motors - motors_horizon.c
 *
 * Purpose: geometric-horizon validation for mount movement paths.
 *
 * The mount must never point below the geometric horizon (MOUNT.md §9).  A
 * slew moves each axis monotonically, so the whole trajectory is contained in
 * the axis-aligned rectangle bounded by the start and destination mechanical
 * coordinates.  Validating the minimum altitude over that rectangle is the
 * conservative check MOUNT.md §10 requires: it rejects some physically valid
 * detours but never admits a path that dips below the horizon.
 *
 * The functions are pure in (mechanical angles, latitude) so both the mount
 * layer (validation at request time) and the motion task (re-validation at
 * execution, when the queued command's start position may have changed) use
 * the same math.
 */

#include "motors.h"

#include <math.h>
#include <stdbool.h>

#define MOTORS_PI 3.14159265358979323846

static double deg_to_rad(double d) { return d * MOTORS_PI / 180.0; }
static double rad_to_deg(double r) { return r * 180.0 / MOTORS_PI; }

/*
 * sin(geometric altitude) for a mechanical position at a site latitude.
 *
 * With φ = |latitude| and the telescope's unit pointing derived from the
 * polar-aligned geometry, the horizon test is the MOUNT.md formula:
 *
 *     sin(altitude) = cos(φ)·sin(D)·cos(R) + sin(φ)·cos(D)
 */
double motors_altitude_sin(float ra_axis_deg, float dec_axis_deg, float lat) {
    double phi = fabs(deg_to_rad((double) lat));
    double R = deg_to_rad((double) ra_axis_deg);
    double D = deg_to_rad((double) dec_axis_deg);
    return cos(phi) * sin(D) * cos(R) + sin(phi) * cos(D);
}

/*
 * Evaluate a candidate (R, D), plus its ±360° periodic shifts in each axis,
 * and fold any instance inside the rectangle into *best.
 */
static void consider(double R, double D,
                     double R_lo, double R_hi, double D_lo, double D_hi,
                     double c, double s, double *best) {
    for (int kr = -1; kr <= 1; kr++) {
        for (int kd = -1; kd <= 1; kd++) {
            double rr = R + 360.0 * kr;
            double dd = D + 360.0 * kd;
            if (rr >= R_lo && rr <= R_hi && dd >= D_lo && dd <= D_hi) {
                double v = c * sin(deg_to_rad(dd)) * cos(deg_to_rad(rr))
                         + s * cos(deg_to_rad(dd));
                if (v < *best) *best = v;
            }
        }
    }
}

/*
 * True when every mechanical position in the axis-aligned rectangle from
 * (ra_from, dec_from) to (ra_to, dec_to) is at or above the geometric horizon.
 *
 * The minimum of a smooth function on a compact rectangle is attained at a
 * corner, on an edge, or at an interior critical point, so all three families
 * are enumerated exactly.
 */
bool motors_path_above_horizon(float ra_from, float dec_from,
                               float ra_to, float dec_to, float lat) {
    double R_lo = fmin((double) ra_from, (double) ra_to);
    double R_hi = fmax((double) ra_from, (double) ra_to);
    double D_lo = fmin((double) dec_from, (double) dec_to);
    double D_hi = fmax((double) dec_from, (double) dec_to);

    double phi = fabs(deg_to_rad((double) lat));
    double c = cos(phi), s = sin(phi);
    double best = INFINITY;

    /* Corners. */
    consider(R_lo, D_lo, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    consider(R_lo, D_hi, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    consider(R_hi, D_lo, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    consider(R_hi, D_hi, R_lo, R_hi, D_lo, D_hi, c, s, &best);

    /* Edge critical points — R pinned at a boundary: extrema of
     * f(D) = c·cos(R)·sin(D) + s·cos(D) = A·sin(D) + s·cos(D). */
    for (int i = 0; i < 2; i++) {
        double Rf = (i == 0) ? R_lo : R_hi;
        double A = c * cos(deg_to_rad(Rf));
        double Dc = rad_to_deg(atan2(A, s));
        consider(Rf, Dc, R_lo, R_hi, D_lo, D_hi, c, s, &best);
        consider(Rf, Dc + 180.0, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    }

    /* Edge critical points — D pinned at a boundary: f(R) is linear in cos(R),
     * so its derivative vanishes at sin(R) = 0 (R a multiple of 180°). */
    for (int i = 0; i < 2; i++) {
        double Df = (i == 0) ? D_lo : D_hi;
        consider(0.0, Df, R_lo, R_hi, D_lo, D_hi, c, s, &best);
        consider(180.0, Df, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    }

    /* Interior critical points — both partial derivatives vanish.
     * sin(R) = 0 with c·cos(D)·cos(R) − s·sin(D) = 0. */
    for (int i = 0; i < 2; i++) {
        double R0 = (i == 0) ? 0.0 : 180.0;
        double A = c * cos(deg_to_rad(R0));
        double Dc = rad_to_deg(atan2(A, s));
        consider(R0, Dc, R_lo, R_hi, D_lo, D_hi, c, s, &best);
        consider(R0, Dc + 180.0, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    }
    /* sin(D) = 0 with c·cos(D)·cos(R) = 0, i.e. cos(R) = 0. */
    for (int i = 0; i < 2; i++) {
        double D0 = (i == 0) ? 0.0 : 180.0;
        consider(90.0, D0, R_lo, R_hi, D_lo, D_hi, c, s, &best);
        consider(270.0, D0, R_lo, R_hi, D_lo, D_hi, c, s, &best);
    }

    return best >= 0.0;
}
