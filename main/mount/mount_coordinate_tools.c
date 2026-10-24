/* Mount - mount_coordinate_tools.c
 *
 * Purpose: convert between physical axis coordinates and equatorial
 * coordinates for a polar-aligned equatorial mount in either hemisphere.
 *
 * The conversions are pure functions of the supplied axis position, site
 * (latitude/longitude), a fractional unix instant, and — for the reverse
 * direction — the configured mechanical limits (a snapshot). They never read
 * mutable global state, so the same inputs always produce the same output
 * regardless of how the mount reached a position.
 *
 * Mechanical conventions:
 *   R = ra_axis_deg, D = dec_axis_deg (absolute final-axis angles, D linear).
 *   Home (R=0, D=0) points at the reference pole:
 *       southern hemisphere (lat < 0): South Celestial Pole, dec = -90.
 *       northern hemisphere (lat > 0): North Celestial Pole, dec = +90.
 *   R positive moves the pointing west and increases the hour angle (H = R).
 *
 * The reference pole is selected by the latitude sign:
 *   p = +1 north (lat > 0), -1 south (lat <= 0). lat == 0 uses the southern
 *   convention (documented and covered by a test).
 *
 * Forward (axis -> equatorial), with Z toward the north celestial pole, X
 * toward H=0/dec=0, Y toward H=+90/dec=0:
 *   x = sin(D) cos(R);  y = sin(D) sin(R);  z = p cos(D)
 *   dec = atan2(z, hypot(x,y));  H = atan2(y,x);  ra = LST - H/15
 *
 * Reverse (equatorial -> axis) enumerates the two geometric families
 *   (A: R = H + 360k,  D = +c + 360m;
 *    B: R = H+180 + 360k, D = -c + 360m,  with c = 90 - p*dec)
 * and picks the minimum |dR| + |dD| from the current position. At a celestial
 * pole (dec = ±90) the RA is indeterminate: the current R is kept and the
 * representation of the REQUESTED pole with the least DEC travel is chosen.
 *
 * When a slew speed is supplied, each candidate is advanced by the sidereal
 * rate over its own travel time before being validated and compared, so a
 * target whose nearest solution would drift past a limit during the move is
 * still reached through the other solution.
 */
#include "mount.h"
#include "mount_internal.h"

#include <math.h>
#include <stdbool.h>

#define MOUNT_PI             3.14159265358979323846
#define MOUNT_POLE_SIN_EPS   1e-6    /* |sin D| < this => pole (≈0.51 microstep) */
#define MOUNT_POLE_COS_EPS   1e-6    /* |cos dec| < this => celestial-pole target */

static double deg_to_rad(double d) { return d * MOUNT_PI / 180.0; }
static double rad_to_deg(double r) { return r * 180.0 / MOUNT_PI; }

/* Normalise RA hours to [0, 24). */
static float normalize_hours(double h) {
    h = fmod(h, 24.0);
    if (h < 0.0) h += 24.0;
    return (float) h;
}

/* Hemisphere pole sign: +1 north (lat > 0), -1 south (lat <= 0). */
static double pole_sign(float lat) {
    return (lat > 0.0f) ? 1.0 : -1.0;
}

/* Quantize a destination angle with the motor's own degrees->steps conversion,
 * clamp it to the nearest step whose DEGREE value stays inside
 * [limit_min, limit_max] (the limits are never rounded outward), and write that
 * degree to *out.  Returns false when the interval contains no representable
 * step (lo > hi) or the float round-trip lands outside the limits. */
static bool quantize_within(double degrees, float limit_min, float limit_max,
                            double step, float *out) {
    int64_t lo = (int64_t) ceil(limit_min / step);
    int64_t hi = (int64_t) floor(limit_max / step);
    if (lo > hi) {
        return false;   /* interval narrower than one step: no valid step */
    }

    int64_t steps = motors_deg_to_steps((float) degrees);
    if (steps < lo) steps = lo;
    if (steps > hi) steps = hi;

    float deg = motors_steps_to_deg(steps);
    if (deg < limit_min || deg > limit_max) {
        return false;   /* float round-trip landed outside the limits */
    }

    *out = deg;
    return true;
}

/* --------------------------------------------------------------------------
 * Axis -> Equatorial  (pure forward conversion)
 * -------------------------------------------------------------------------- */
EquatorialCoordinates axis_to_equatorial(AxisCoordinates axis, float lat, float lon,
                                         double unix_seconds) {
    EquatorialCoordinates out;
    double p = pole_sign(lat);

    double R = deg_to_rad(axis.ra_axis_deg);
    double D = deg_to_rad(axis.dec_axis_deg);
    double sD = sin(D);
    double x = sD * cos(R);
    double y = sD * sin(R);
    double z = p * cos(D);

    double dec_deg = rad_to_deg(atan2(z, hypot(x, y)));
    double lst = mount_get_lst_at(lon, unix_seconds);

    double ra_hours;
    if (fabs(sD) < MOUNT_POLE_SIN_EPS) {
        /* Celestial pole: RA is indeterminate. Finite, deterministic convention
         * derived from the current position: ra = LST - R/15. */
        ra_hours = lst - axis.ra_axis_deg / 15.0;
    } else {
        double H = rad_to_deg(atan2(y, x));
        ra_hours = lst - H / 15.0;
    }

    out.ra_hours = normalize_hours(ra_hours);
    out.dec_deg = (float) dec_deg;
    return out;
}

/* --------------------------------------------------------------------------
 * Equatorial -> Axis (reverse, minimum-travel selection with arrival
 * compensation per candidate)
 * -------------------------------------------------------------------------- */
bool mount_select_axis_solution(EquatorialCoordinates eq, AxisCoordinates current,
                                float lat, float lon, double unix_seconds,
                                float speed_dps, const MountAxisLimits *limits,
                                bool *is_pole, AxisCoordinates *out) {
    if (!out || !limits || !isfinite(eq.ra_hours) || !isfinite(eq.dec_deg) ||
        !isfinite(current.ra_axis_deg) || !isfinite(current.dec_axis_deg) ||
        eq.dec_deg < -90.0f || eq.dec_deg > 90.0f) {
        return false;
    }

    double p = pole_sign(lat);
    double dec = eq.dec_deg;
    double step = motors_steps_to_deg(1);   /* one microstep: tie tolerance + quantization */

    /* Celestial-pole target: RA is arbitrary. Keep the current R (if valid) and
     * pick the representation of the REQUESTED pole with the least DEC travel.
     * A request for the north pole must not fall back to D=0 (the south pole)
     * just because it is closer. */
    if (fabs(cos(deg_to_rad(dec))) < MOUNT_POLE_COS_EPS) {
        double c = 90.0 - p * dec;               /* ~0 (reference) or ~180 (opposite) */
        double d_base = (c < 90.0) ? 0.0 : 180.0;
        int m_min = (int) ceil((limits->dec_min - d_base) / 360.0);
        int m_max = (int) floor((limits->dec_max - d_base) / 360.0);

        double best_d = NAN, best_cost = INFINITY;
        for (int m = m_min; m <= m_max; m++) {
            double D = d_base + 360.0 * m;
            double cost = fabs(D - current.dec_axis_deg);
            if (cost < best_cost) { best_cost = cost; best_d = D; }
        }
        if (!isfinite(best_d)) return false;
        if (current.ra_axis_deg < limits->ra_min || current.ra_axis_deg > limits->ra_max) {
            return false;
        }
        float d_q;
        if (!quantize_within(best_d, limits->dec_min, limits->dec_max, step, &d_q)) {
            return false;
        }
        if (is_pole) *is_pole = true;
        out->ra_axis_deg = current.ra_axis_deg;
        out->dec_axis_deg = d_q;
        return true;
    }

    double c = 90.0 - p * dec;
    double alpha = fmod(eq.ra_hours, 24.0);
    if (alpha < 0.0) alpha += 24.0;
    double lst = mount_get_lst_at(lon, unix_seconds);
    double H = 15.0 * (lst - alpha);   /* degrees */

    bool found = false;
    double best_cost = INFINITY, best_dr = INFINITY, best_dd = INFINITY;
    double best_ra = NAN, best_dec = NAN;

    for (int b = 0; b <= 1; b++) {
        double r0 = H + 180.0 * b;
        double d0 = (b == 0) ? c : -c;
        /* The lower k bound is widened by one so a candidate whose request-time
         * RA is just below ra_min (but whose arrival-compensated RA lands inside
         * the limits) is still considered and filtered below. */
        int k_min = (int) ceil((limits->ra_min - r0) / 360.0) - 1;
        int k_max = (int) floor((limits->ra_max - r0) / 360.0);
        int m_min = (int) ceil((limits->dec_min - d0) / 360.0);
        int m_max = (int) floor((limits->dec_max - d0) / 360.0);

        for (int k = k_min; k <= k_max; k++) {
            double R = r0 + 360.0 * k;   /* RA axis at the request time */
            for (int m = m_min; m <= m_max; m++) {
                double D = d0 + 360.0 * m;

                /* The sky advances during the slew: each candidate is advanced
                 * by the sidereal rate over ITS OWN travel time, then validated
                 * against the limits before the minimum-travel choice. */
                double dr = fabs(R - current.ra_axis_deg);
                double dd = fabs(D - current.dec_axis_deg);
                double travel = (dr > dd) ? dr : dd;
                double duration = (speed_dps > 0.0) ? travel / speed_dps + 2.0 : 0.0;
                double R_arr = R + TRACKING_SPEED_SIDEREAL_DPS * duration;

                if (R_arr < limits->ra_min || R_arr > limits->ra_max) continue;

                /* Quantize with the motor's own degrees->steps conversion and
                 * clamp to the nearest interior step; discard the candidate if
                 * the interval holds no representable step. The cost is compared
                 * on the float values, not the quantized ones. */
                float R_q, D_q;
                if (!quantize_within(R_arr, limits->ra_min, limits->ra_max, step, &R_q) ||
                    !quantize_within(D, limits->dec_min, limits->dec_max, step, &D_q)) {
                    continue;
                }

                double dr_arr = fabs(R_arr - current.ra_axis_deg);
                double cost = dr_arr + dd;

                bool better;
                if (fabs(cost - best_cost) > step) {
                    better = cost < best_cost;
                } else if (fabs(dr_arr - best_dr) > step) {
                    better = dr_arr < best_dr;
                } else if (fabs(dd - best_dd) > step) {
                    better = dd < best_dd;
                } else {
                    /* Fully tied: fixed order wins (A before B, k asc, m asc). */
                    better = false;
                }

                if (better) {
                    best_cost = cost; best_dr = dr_arr; best_dd = dd;
                    best_ra = R_q; best_dec = D_q;
                    found = true;
                }
            }
        }
    }

    if (!found) return false;
    if (is_pole) *is_pole = false;
    out->ra_axis_deg = (float) best_ra;
    out->dec_axis_deg = (float) best_dec;
    return true;
}

bool equatorial_to_axis(EquatorialCoordinates eq, AxisCoordinates current,
                        float lat, float lon, double unix_seconds,
                        AxisCoordinates *out) {
    MotorsState ms = motors_current_state();
    MountAxisLimits limits = { ms.limits.ra_min, ms.limits.ra_max,
                               ms.limits.dec_min, ms.limits.dec_max };
    return mount_select_axis_solution(eq, current, lat, lon, unix_seconds,
                                      0.0f, &limits, NULL, out);
}

/* =========================================================================
 * HMS / DMS formatting
 * ========================================================================= */

RaHMS ra_hours_to_hms(float ra_hours) {
    RaHMS out;
    float total = fabsf(ra_hours);
    out.hours   = (int) total;
    float rem   = (total - (float) out.hours) * 60.0f;
    out.minutes = (int) rem;
    out.seconds = (rem - (float) out.minutes) * 60.0f;
    return out;
}

DecDMS dec_deg_to_dms(float dec_deg) {
    DecDMS out;
    out.sign    = (dec_deg >= 0.0f) ? 1 : -1;
    float total = fabsf(dec_deg);
    out.degrees = (int) total;
    float rem   = (total - (float) out.degrees) * 60.0f;
    out.minutes = (int) rem;
    out.seconds = (rem - (float) out.minutes) * 60.0f;
    return out;
}
