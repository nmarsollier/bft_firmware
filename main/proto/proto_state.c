/* Proto — proto_state.c — STATE: a coherent snapshot of the whole mount.
 *
 * Everything is read from the motors/mount layer in a single pass so the
 * snapshot is internally consistent, and fields that cannot be computed are
 * reported as invalid rather than as plausible-looking zeros (see the plan).
 */
#include "proto_internal.h"

#include <math.h>

#include "esp_timer.h"
#include "power.h"

static const char *pier_side_name(int pier_side) {
    return pier_side == 0 ? "east" : "west";
}

void proto_state_snapshot(char *out, size_t cap, uint32_t id) {
    MotorsState s = motors_current_state();
    float ra_axis = motors_steps_to_deg(s.ra_steps);
    float dec_axis = motors_steps_to_deg(s.dec_steps);

    AxisCoordinates axis = { .ra_axis_deg = ra_axis, .dec_axis_deg = dec_axis };
    EquatorialCoordinates eq = axis_to_equatorial(axis);

    int pier = (dec_axis >= 0.0f) ? 0 : 1;
    bool at_home = (s.status == MOTORS_STATUS_READY)
                && fabsf(ra_axis) < 1.0f
                && fabsf(dec_axis) < 1.0f;
    bool guiding = s.guiding;

    size_t len = 0;
    out[0] = '\0';

    proto_buf_append(out, cap, &len,
        "{\"type\":\"state\",\"id\":%lu,\"ok\":true,"
        "\"boot_id\":%lu,"
        "\"state\":\"%s\",\"tracking\":\"%s\",",
        (unsigned long) id,
        (unsigned long) proto_boot_id(),
        motors_status_to_string(s.status),
        motors_tracking_to_string(s.tracking));

    proto_buf_append(out, cap, &len,
        "\"ra_steps\":%lld,\"dec_steps\":%lld,"
        "\"ra_axis_deg\":%.4f,\"dec_axis_deg\":%.4f,"
        "\"ra\":%.4f,\"dec\":%.4f,\"lst\":%.4f,\"pier_side\":\"%s\",",
        (long long) s.ra_steps, (long long) s.dec_steps,
        ra_axis, dec_axis,
        eq.ra_hours, eq.dec_deg, mount_get_lst(), pier_side_name(pier));

    proto_buf_append(out, cap, &len,
        "\"ra_speed_dps\":%.6f,\"dec_speed_dps\":%.6f,"
        "\"guiding\":%s,\"at_home\":%s,\"at_park\":%s,"
        "\"power\":%s,\"time_valid\":%s,",
        s.ra_speed, s.dec_speed,
        guiding ? "true" : "false",
        at_home ? "true" : "false",
        (s.status == MOTORS_STATUS_PARKED) ? "true" : "false",
        power_has_external() ? "true" : "false",
        mount_time_is_valid() ? "true" : "false");

    proto_buf_append(out, cap, &len,
        "\"limits\":{\"ra_min\":%.4f,\"ra_max\":%.4f,\"dec_min\":%.4f,\"dec_max\":%.4f},"
        "\"config_rev\":%lu,\"uptime_ms\":%llu}",
        s.limits.ra_min, s.limits.ra_max, s.limits.dec_min, s.limits.dec_max,
        (unsigned long) proto_config_rev(),
        (unsigned long long) (esp_timer_get_time() / 1000));
}

void proto_handle_state(const char *req, uint32_t id, char *out, size_t cap) {
    (void) req;
    proto_state_snapshot(out, cap, id);
}
