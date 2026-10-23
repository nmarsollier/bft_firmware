/* Mount - mount_coordinate_tools.c
 *
 * Purpose: convert between physical axis coordinates and equatorial
 * coordinates for a fork equatorial mount in the southern hemisphere.
 *
 * =========================================================================
 * MECHANICAL MODEL
 * =========================================================================
 *
 * Home position (mechanical zero):
 *   - Scope points at the South Celestial Pole (SCP).
 *   - ra_axis = 0, dec_axis = 0.
 *   - Reported celestial coordinates: RA = LST, DEC = -90.
 *
 * In a fork mount the DEC movement plane is already aligned with the
 * meridian at mechanical home: moving only DEC sweeps the scope toward
 * the zenith with no RA pre-rotation.  A German EQ needs RA at roughly
 * +/-90 from home to achieve the same alignment.
 *
 * The hour-angle offset at which the DEC plane aligns with the meridian
 * is a fixed mechanical constant: HOME_HA_OFFSET_DEG = 0 (90 for a
 * German EQ).
 *
 * =========================================================================
 * FORK ORIENTATION
 * =========================================================================
 *
 * A fork mount has no side of pier and no meridian flip.  GOTO preserves the
 * physical DEC orientation: positive at home, negative only when the telescope
 * was moved through the pole by hand.  It never selects the opposite
 * orientation to reach a target.
 *
 *   positive DEC: ra = HA,       dec =  DEC + 90
 *   negative DEC: ra = HA + 180, dec = -(DEC + 90)
 *
 * In the negative branch the +180° converts the already-existing physical
 * orientation; it is NOT a command to flip.  DEC is a linear coordinate (not
 * wrapped): its sign is still needed for coordinate conversion and for
 * North/South guiding, even though a fork has no pier side.
 *
 * =========================================================================
 * COORDINATE SYSTEM
 * =========================================================================
 *
 * Equatorial (RA, DEC):
 *   RA  in decimal hours   [0, 24)
 *   DEC in decimal degrees  [-90, +90]   (+90 = north pole)
 *
 * Axis (ra_axis, dec_axis):
 *   ra_axis  = rotation around polar axis, degrees [-180, +180).
 *   dec_axis = tilt from polar axis, degrees, linear (NOT wrapped).
 *              0 = scope parallel to RA axis (SCP, DEC=-90).
 *              +90 = scope at celestial equator (DEC=0).
 *
 * Hour Angle (HA):
 *   HA = LST - RA   (hours, wrapped to [-12, +12))
 *   HA > 0  ->  target is west of the meridian.
 *   HA < 0  ->  target is east of the meridian.
 *
 * =========================================================================
 * CONSTANTS
 * =========================================================================
 *
 * HOME_HA_OFFSET_DEG (0.0)
 *   Hour-angle offset at which the DEC plane aligns with the meridian.
 *
 * Axis limits (±150° RA, ±150° DEC) are defined in motors_limits.c
 * via motors_state.limits and validated at runtime through
 * motors_current_state().limits.
 *   When |dec_axis| < 1 the scope is at the pole; RA is singular.
 *
 * =========================================================================
 * ASTRONOMICAL FORMULAS
 * =========================================================================
 *
 * Julian Date:  JD = unix_time / 86400 + 2440587.5
 *   86400 = seconds/day.  2440587.5 = JD at Unix epoch.
 *
 * GMST (USNO, ~1 arcsec):
 *   T = (JD - 2451545.0) / 36525.0   — centuries from J2000.0
 *   GMST = 280.46061837 + 360.98564736629*(JD-2451545.0)
 *        + 0.000387933*T^2 - T^3/38710000.0   (degrees)
 *   GMST_h = GMST / 15   (hours)
 *
 * LST = GMST + lon / 15
 * HA  = LST - RA   (hours, [-12, +12))
 * RA  = LST - HA   (hours, [0, 24))
 */
#include "mount.h"
#include "mount_internal.h"

#include <math.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <esp_log.h>

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static double unix_time_to_julian_date(double t) {
    return t / 86400.0 + 2440587.5;
}

static double gmst_hours_from_jd(double jd) {
    double T = (jd - 2451545.0) / 36525.0;
    double gmst_deg = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                      + 0.000387933 * T * T - (T * T * T) / 38710000.0;
    gmst_deg = fmod(gmst_deg, 360.0);
    if (gmst_deg < 0.0) gmst_deg += 360.0;
    return gmst_deg / 15.0;
}

/* Normalise a circular angle to [-180, +180).  +180 -> -180. */
static float normalize_degf(float d) {
    d = fmodf(d, 360.0f);
    if (d >= 180.0f)  d -= 360.0f;
    if (d < -180.0f)  d += 360.0f;
    return d;
}

/* Normalise hours to [0, 24). */
static float normalize_hoursf(float h) {
    while (h < 0.0f)   h += 24.0f;
    while (h >= 24.0f) h -= 24.0f;
    return h;
}

/* Hour-angle offset at which the DEC plane aligns with the meridian.
 * Fork mount: 0 (DEC opens along the meridian at RA home).  German EQ: 90. */
#define HOME_HA_OFFSET_DEG  0.0f

/* Validate against motor hardware limits (single source of truth).
 * motors_current_state().limits defines the authoritative boundaries. */
static bool axis_within_limits(float ra, float dec) {
    MotorsState ms = motors_current_state();
    return (ra >= ms.limits.ra_min && ra <= ms.limits.ra_max)
        && (dec >= ms.limits.dec_min && dec <= ms.limits.dec_max);
}

/* --------------------------------------------------------------------------
 * Equatorial -> Axis  (forward conversion)
 *
 * Keep the current DEC orientation; reject targets outside the configured
 * travel rather than switching orientation or adding full RA revolutions.
 * -------------------------------------------------------------------------- */
bool equatorial_to_axis(EquatorialCoordinates eq, AxisCoordinates current,
                        time_t at_time, AxisCoordinates *out) {

    if (!out || !isfinite(eq.ra_hours) || !isfinite(eq.dec_deg) ||
        !isfinite(current.ra_axis_deg) || !isfinite(current.dec_axis_deg) ||
        eq.dec_deg < -90.0f || eq.dec_deg > 90.0f) {
        ESP_LOGE("COORD_CONV", "Invalid DEC %.4f — rejecting", eq.dec_deg);
        return false;
    }

    eq.ra_hours = fmodf(eq.ra_hours, 24.0f);
    if (eq.ra_hours < 0.0f) eq.ra_hours += 24.0f;

    double lst  = gmst_hours_from_jd(unix_time_to_julian_date(at_time))
                  + (mount_internal_state.lon / 15.0);

    float ha_h = (float)(lst - eq.ra_hours);
    while (ha_h > 12.0f)   ha_h -= 24.0f;
    while (ha_h < -12.0f)  ha_h += 24.0f;

    float ha_deg = ha_h * 15.0f;
    float dec_base = eq.dec_deg + 90.0f;

    /* The orientation is a remembered physical state, not the current DEC sign:
     * at dec_axis == 0 (the pole) the sign is lost, and a GOTO out of the pole
     * must use the previous side instead of swinging 180°. */
    bool negative_dec = motors_current_state().dec_orientation_negative;
    float ra = normalize_degf(ha_deg - HOME_HA_OFFSET_DEG
                             + (negative_dec ? 180.0f : 0.0f));
    float dec = negative_dec ? -dec_base : dec_base;

    /* At the celestial pole RA is arbitrary; keep the polar axis stationary. */
    if (dec_base == 0.0f) ra = current.ra_axis_deg;

    if (!axis_within_limits(ra, dec)) {
        ESP_LOGE("COORD_CONV", "Target outside fork travel: RA=%.4fh DEC=%.4f",
                 eq.ra_hours, eq.dec_deg);
        return false;
    }

    out->ra_axis_deg = ra;
    out->dec_axis_deg = dec;

    return true;
}

/* --------------------------------------------------------------------------
 * Axis -> Equatorial  (reverse conversion)
 *
 * The orientation is the remembered physical state (motors_state
 * .dec_orientation_negative), not the sign of dec_axis — at dec_axis == 0 the
 * sign is ambiguous:
 *
 *   positive  ->  HA = ra + offset,       DEC = dec - 90
 *   negative  ->  HA = ra + offset + 180, DEC = -dec - 90
 * -------------------------------------------------------------------------- */
EquatorialCoordinates axis_to_equatorial(AxisCoordinates axis) {
    EquatorialCoordinates out;

    /* DEC is computed directly for every position.  At dec_axis == 0 (the
     * exact pole) DEC is -90 and RA is degenerate: the formula still returns
     * a valid, if arbitrary, RA — no threshold erases the nearby cone. */
    float ha_deg, dec_deg;
    if (!motors_current_state().dec_orientation_negative) {
        /* Positive orientation: HA = ra + offset. */
        ha_deg  = normalize_degf(axis.ra_axis_deg + HOME_HA_OFFSET_DEG);
        dec_deg = axis.dec_axis_deg - 90.0f;
    } else {
        /* Negative orientation: HA = ra + offset + 180. */
        ha_deg  = normalize_degf(axis.ra_axis_deg + HOME_HA_OFFSET_DEG + 180.0f);
        dec_deg = -axis.dec_axis_deg - 90.0f;
    }

    float ha_h = ha_deg / 15.0f;

    /* Fractional-second clock — avoids the ~15"/s sawtooth in reported RA. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double now = (double) tv.tv_sec + (double) tv.tv_usec / 1e6;

    double lst = gmst_hours_from_jd(unix_time_to_julian_date(now))
                 + (mount_internal_state.lon / 15.0);

    out.ra_hours = normalize_hoursf((float)(lst - ha_h));

    if (dec_deg < -90.0f) dec_deg = -90.0f;
    if (dec_deg >  90.0f) dec_deg =  90.0f;
    out.dec_deg = dec_deg;

    return out;
}

/* =========================================================================
 * HMS / DMS formatting
 * ========================================================================= */

RaHMS ra_hours_to_hms(float ra_hours) {
    RaHMS out;
    float total = fabsf(ra_hours);
    out.hours   = (int)total;
    float rem   = (total - (float)out.hours) * 60.0f;
    out.minutes = (int)rem;
    out.seconds = (rem - (float)out.minutes) * 60.0f;
    return out;
}

DecDMS dec_deg_to_dms(float dec_deg) {
    DecDMS out;
    out.sign    = (dec_deg >= 0.0f) ? 1 : -1;
    float total = fabsf(dec_deg);
    out.degrees = (int)total;
    float rem   = (total - (float)out.degrees) * 60.0f;
    out.minutes = (int)rem;
    out.seconds = (rem - (float)out.minutes) * 60.0f;
    return out;
}
