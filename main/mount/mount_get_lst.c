/* Mount - mount_get_lst.c
 *
 * Purpose: compute the current Local Sidereal Time for the configured site.
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

float mount_get_lst(void) {
    /* Fractional-second clock — avoids the ~15"/s sawtooth in reported RA. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double now = (double) tv.tv_sec + (double) tv.tv_usec / 1e6;

    double jd = jd_from_unix(now);
    double gmst_h = gmst_hours(jd);
    double lst_h = gmst_h + (mount_internal_state.lon / 15.0);

    /* Normalize to [0, 24). */
    while (lst_h < 0.0)  lst_h += 24.0;
    while (lst_h >= 24.0) lst_h -= 24.0;

    return (float) lst_h;
}
