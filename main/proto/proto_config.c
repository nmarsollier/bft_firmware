/* Proto — proto_config.c — CONFIG: read and update persistent configuration.
 *
 * A request with no config fields is a read; the response echoes the current
 * config.  Any recognised field updates only that field (partial update) and,
 * on success, bumps the persisted config revision.  Time (`utc`) is applied
 * immediately but not persisted — it is the desktop app's job to keep the
 * mount's clock set.
 */
#include "proto_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void record_error(const MountResult *r, char *err, size_t cap) {
    if (!r->ok && err[0] == '\0') {
        snprintf(err, cap, "%s", r->message);
    }
}

/* Format the current system clock as UTC ISO 8601 ("2026-09-28T02:00:00Z"). */
static void format_utc_now(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm tm = {0};
    gmtime_r(&now, &tm);
    strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void proto_handle_config(const char *req, uint32_t id, char *out, size_t cap) {
    char err[128] = {0};
    bool changed = false;

    /* ── Parse and validate every field before applying any change ──
     *
     * `proto_json_has_key` distinguishes "absent" from "present"; a field that
     * is present but has the wrong type or a non-finite value must reject the
     * whole request, not be silently treated as absent. */
    MountSettings settings = mount_get_visible_status().settings;
    float f;
    int iv;
    bool site_changed = false;

    bool lat_present = proto_json_has_key(req, "lat");
    bool lon_present = proto_json_has_key(req, "lon");
    bool elev_present = proto_json_has_key(req, "elevation");

    if (err[0] == '\0' && lat_present) {
        if (!proto_json_get_float(req, "lat", &f)) {
            snprintf(err, sizeof(err), "invalid lat");
        } else {
            settings.lat = f;
            site_changed = true;
        }
    }
    if (err[0] == '\0' && lon_present) {
        if (!proto_json_get_float(req, "lon", &f)) {
            snprintf(err, sizeof(err), "invalid lon");
        } else {
            settings.lon = f;
            site_changed = true;
        }
    }
    if (err[0] == '\0' && elev_present) {
        if (!proto_json_get_int(req, "elevation", &iv)) {
            snprintf(err, sizeof(err), "invalid elevation");
        } else {
            settings.elevation = iv;
            site_changed = true;
        }
    }

    /* Site range validation — must run BEFORE any apply, otherwise an
     * out-of-range latitude/longitude would still apply the UTC clock and bump
     * the revision (a partial effect). */
    if (err[0] == '\0' && site_changed) {
        MountResult vr = mount_settings_validate(settings);
        record_error(&vr, err, sizeof(err));
    }

    float ra_rate = 0.0f;
    float dec_rate = 0.0f;
    bool ra_present = proto_json_has_key(req, "guide_rate_ra");
    bool dec_present = proto_json_has_key(req, "guide_rate_dec");

    if (err[0] == '\0' && ra_present) {
        if (!proto_json_get_float(req, "guide_rate_ra", &ra_rate) ||
            !mount_guide_rate_is_valid(ra_rate)) {
            snprintf(err, sizeof(err), "guide rate out of range [0,1]");
        }
    }
    if (err[0] == '\0' && dec_present) {
        if (!proto_json_get_float(req, "guide_rate_dec", &dec_rate) ||
            !mount_guide_rate_is_valid(dec_rate)) {
            snprintf(err, sizeof(err), "guide rate out of range [0,1]");
        }
    }

    char utc[64];
    bool utc_present = proto_json_has_key(req, "utc");
    if (err[0] == '\0' && utc_present) {
        if (!proto_json_get_string(req, "utc", utc, sizeof(utc))) {
            snprintf(err, sizeof(err), "invalid utc");
        }
    }

    /* ── Apply only if every field validated cleanly ──
     *
     * UTC is applied first: its validation can only fail inside
     * mount_set_system_time, and applying it first guarantees a bad UTC leaves
     * no partial effect on site or guide rates. */
    if (err[0] == '\0' && utc_present) {
        MountResult r = mount_set_system_time(utc);
        record_error(&r, err, sizeof(err));
        if (r.ok) changed = true;
    }
    if (err[0] == '\0' && site_changed) {
        MountResult r = mount_settings_update(settings);
        record_error(&r, err, sizeof(err));
        if (r.ok) changed = true;
    }
    if (err[0] == '\0' && ra_present) {
        mount_set_guide_rate_ra(ra_rate);
        changed = true;
    }
    if (err[0] == '\0' && dec_present) {
        mount_set_guide_rate_dec(dec_rate);
        changed = true;
    }

    if (changed) {
        proto_config_rev_bump();
    }

    size_t len = 0;
    out[0] = '\0';

    if (err[0] != '\0') {
        proto_buf_append(out, cap, &len,
            "{\"type\":\"config\",\"id\":%lu,\"ok\":false,\"error\":\"%s\"}",
            (unsigned long) id, err);
        return;
    }

    MountSettings cur = mount_get_visible_status().settings;
    char utc_buf[32];
    format_utc_now(utc_buf, sizeof(utc_buf));
    proto_buf_append(out, cap, &len,
        "{\"type\":\"config\",\"id\":%lu,\"ok\":true,\"config_rev\":%lu,"
        "\"utc\":\"%s\","
        "\"config\":{\"lat\":%.4f,\"lon\":%.4f,\"elevation\":%d,"
        "\"guide_rate_ra\":%.6f,\"guide_rate_dec\":%.6f}}",
        (unsigned long) id, (unsigned long) proto_config_rev(),
        utc_buf,
        cur.lat, cur.lon, cur.elevation,
        mount_get_guide_rate_ra(), mount_get_guide_rate_dec());
}
