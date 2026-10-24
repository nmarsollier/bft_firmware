/* Mount - mount_get_lst.c
 *
 * Purpose: compute the Local Sidereal Time for a site longitude and a
 * fractional unix instant.
 */
#include "mount.h"
#include "mount_internal.h"

#include <math.h>
#include <sys/time.h>
#include <time.h>

static double jd_from_unix(double t) {
    return t / 86400.0 + 2440587.5;
}

static double gmst_hours(double jd) {
    double T = (jd - 2451545.0) / 36525.0;
    double gmst_deg = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                      + 0.000387933 * T * T - (T * T * T) / 38710000.0;
    gmst_deg = fmod(gmst_deg, 360.0);
    if (gmst_deg < 0.0) gmst_deg += 360.0;
    return gmst_deg / 15.0;
}

/* Fractional unix instant (seconds with sub-second precision).  Used by the
 * pure conversions and status reporting so a coherent snapshot does not mix
 * instants (the integer-only time_t would introduce a ~15"/s RA sawtooth). */
double mount_unix_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + (double) tv.tv_usec / 1e6;
}

float mount_get_lst_at(float lon, double unix_seconds) {
    double gmst_h = gmst_hours(jd_from_unix(unix_seconds));
    double lst_h = gmst_h + (lon / 15.0);
    lst_h = fmod(lst_h, 24.0);
    if (lst_h < 0.0) lst_h += 24.0;
    return (float) lst_h;
}

float mount_get_lst(void) {
    return mount_get_lst_at(mount_internal_state.lon, mount_unix_now());
}
