/* Proto — proto_control.c — CONTROL: desired continuous motion.
 *
 * Semantics: an omitted field keeps its current value; a manual rate of zero
 * stops that axis.  The whole request is validated before anything is applied,
 * so an invalid field cannot leave a partial side effect — an invalid rate
 * cannot silently stop an axis, and tracking is not applied before a later
 * field fails.
 */
#include "proto_internal.h"

#include <stdio.h>
#include <string.h>

void proto_handle_control(const char *req, uint32_t id, char *out, size_t cap) {
    char err[128] = {0};

    /* ── Parse and validate everything first ── */
    bool has_tracking = proto_json_has_key(req, "tracking");
    bool has_ra = proto_json_has_key(req, "manual_ra_dps");
    bool has_dec = proto_json_has_key(req, "manual_dec_dps");

    TrackingMode mode = TRACKING_NONE;
    if (has_tracking) {
        char value[16];
        if (!proto_json_get_string(req, "tracking", value, sizeof(value))) {
            snprintf(err, sizeof(err), "invalid tracking");
        } else {
            mode = motors_tracking_from_string(value);
            if (strcmp(value, motors_tracking_to_string(mode)) != 0) {
                snprintf(err, sizeof(err), "invalid tracking");
            }
        }
    }

    float ra = 0.0f;
    float dec = 0.0f;
    if (has_ra && !proto_json_get_float(req, "manual_ra_dps", &ra)) {
        if (err[0] == '\0') snprintf(err, sizeof(err), "invalid manual_ra_dps");
    }
    if (has_dec && !proto_json_get_float(req, "manual_dec_dps", &dec)) {
        if (err[0] == '\0') snprintf(err, sizeof(err), "invalid manual_dec_dps");
    }

    /* ── Apply only if every present field parsed cleanly ── */
    if (err[0] == '\0' && has_tracking) {
        MountResult r = mount_set_tracking(mode);
        if (!r.ok) snprintf(err, sizeof(err), "%s", r.message);
    }

    if (err[0] == '\0' && (has_ra || has_dec)) {
        MountResult r;
        if (has_ra && has_dec) {
            r = mount_set_move_axis_speed(ra, dec);
        } else if (has_ra) {
            r = mount_set_move_axis_rate(0, ra);
        } else {
            r = mount_set_move_axis_rate(1, dec);
        }
        if (!r.ok) snprintf(err, sizeof(err), "%s", r.message);
    }

    size_t len = 0;
    out[0] = '\0';
    if (err[0] != '\0') {
        proto_buf_append(out, cap, &len,
            "{\"type\":\"control\",\"id\":%lu,\"ok\":false,\"error\":\"%s\"}",
            (unsigned long) id, err);
    } else {
        proto_buf_append(out, cap, &len,
            "{\"type\":\"control\",\"id\":%lu,\"ok\":true}", (unsigned long) id);
    }
}
