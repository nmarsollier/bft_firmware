/* Mount - mount_set_system_time.c
 *
 * Purpose: parse time values and update the system clock.
 */
#include "mount.h"
#include "mount_internal.h"

#include "esp_log.h"
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "MOUNT_SET_SYSTEM_TIME";

/* True once a valid system time has been set — gates GOTO. */
bool mount_time_valid = false;

/* Parse an ISO-8601 timestamp into a UTC epoch.  Strict: rejects trailing
 * garbage, out-of-range fields, and dates that would be normalised (Feb 30).
 * Accepts "Z" or "±HH:MM" offsets; a bare timestamp is treated as UTC. */
static bool parse_iso8601_to_time(const char *s, time_t *out) {
    if (s == NULL || out == NULL)
        return false;

    int Y, M, D, h, mi, sec, consumed = 0;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &mi, &sec, &consumed) != 6) {
        return false;
    }
    const char *p = s + consumed;

    /* Optional fractional seconds (".123", ".123456789") — ISO 8601 allows
     * them and JS toISOString() emits three digits. */
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9')
            return false; /* "." must be followed by at least one digit */
        while (*p >= '0' && *p <= '9')
            p++;
    }

    int off_sign = 0, off_h = 0, off_m = 0;
    if (*p == 'Z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        off_sign = (*p == '-') ? -1 : 1;
        p++;
        int c2 = 0;
        if (sscanf(p, "%2d:%2d%n", &off_h, &off_m, &c2) != 2) {
            return false;
        }
        p += c2;
    }
    if (*p != '\0') {
        return false; /* trailing garbage */
    }

    /* Range validation before any normalisation. */
    if (M < 1 || M > 12 || D < 1 || D > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return false;
    if (off_h < 0 || off_h > 23 || off_m < 0 || off_m > 59) return false;

    struct tm tm = {0};
    tm.tm_year = Y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = sec;

    time_t t_utc;
#if defined(__GLIBC__) || defined(_GNU_SOURCE) || defined(__USE_GNU) || defined(__APPLE__)
    extern time_t timegm(struct tm *tm);
    t_utc = timegm(&tm);
#else
    char old_tz_buf[128] = {0};
    char *old_tz = getenv("TZ");
    if (old_tz) {
        strncpy(old_tz_buf, old_tz, sizeof(old_tz_buf) - 1);
    }
    setenv("TZ", "UTC0", 1);
    tzset();
    t_utc = mktime(&tm);
    if (old_tz) {
        setenv("TZ", old_tz_buf, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
#endif

    /* Reject dates that were normalised (e.g. Feb 30 → Mar 1).  Compare the
     * ORIGINAL parsed fields — timegm/mktime normalise the input struct. */
    struct tm check;
    gmtime_r(&t_utc, &check);
    if (check.tm_year != (Y - 1900) || check.tm_mon != (M - 1) ||
        check.tm_mday != D || check.tm_hour != h ||
        check.tm_min != mi || check.tm_sec != sec) {
        return false;
    }

    *out = t_utc - (off_sign * (off_h * 3600 + off_m * 60));
    return true;
}

MountResult mount_set_system_time(const char *iso_time) {
    if (iso_time == NULL || iso_time[0] == '\0') {
        return mount_result_error("Missing time");
    }

    time_t new_time = 0;
    if (!parse_iso8601_to_time(iso_time, &new_time)) {
        return mount_result_error("Invalid time format");
    }

    struct timeval tv = {.tv_sec = new_time, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "Failed to set system time");
        return mount_result_error("Failed to set system time");
    }
    mount_time_valid = true;
    return mount_result_ok();
}

bool mount_time_is_valid(void) {
    return mount_time_valid;
}
