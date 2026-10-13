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

    /* Site location — partial: start from the current value, override present. */
    MountSettings settings = mount_get_visible_status().settings;
    float f;
    int iv;
    bool site_changed = false;

    if (proto_json_get_float(req, "lat", &f)) { settings.lat = f; site_changed = true; }
    if (proto_json_get_float(req, "lon", &f)) { settings.lon = f; site_changed = true; }
    if (proto_json_get_int(req, "elevation", &iv)) { settings.elevation = iv; site_changed = true; }

    if (site_changed) {
        MountResult r = mount_settings_update(settings);
        record_error(&r, err, sizeof(err));
        if (r.ok) changed = true;
    }

    /* Guide rates (in-memory). */
    if (proto_json_get_float(req, "guide_rate_ra", &f)) { mount_set_guide_rate_ra(f); changed = true; }
    if (proto_json_get_float(req, "guide_rate_dec", &f)) { mount_set_guide_rate_dec(f); changed = true; }

    /* UTC time — applied, not persisted. */
    char utc[64];
    if (proto_json_get_string(req, "utc", utc, sizeof(utc))) {
        MountResult r = mount_set_system_time(utc);
        record_error(&r, err, sizeof(err));
        if (r.ok) changed = true;
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
