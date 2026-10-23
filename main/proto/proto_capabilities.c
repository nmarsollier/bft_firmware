/* Proto — proto_capabilities.c — CAPABILITIES: unified handshake + device-specific capabilities.
 *
 * The response uses the shared identification header (device_type, device_id,
 * name, firmware_version, proto, boot_id) so a client can tell the mount from
 * the accessories controller with one parser. The `capabilities` object then
 * holds the mount-specific capabilities.
 */
#include "proto_internal.h"

#include "config.h"

void proto_handle_capabilities(const char *req, uint32_t id, char *out, size_t cap) {
    (void) req;

    size_t len = 0;
    out[0] = '\0';

    proto_buf_append(out, cap, &len,
        "{\"type\":\"capabilities\",\"id\":%lu,\"ok\":true,"
        "\"boot_id\":\"%08lx\",\"proto\":1,"
        "\"device_type\":\"mount\",\"device_id\":\"%s\","
        "\"name\":\"%s\",\"firmware_version\":\"%s\","
        "\"capabilities\":{\"axes\":[\"ra\",\"dec\"],\"slew_speeds_dps\":[",
        (unsigned long) id, (unsigned long) proto_boot_id(),
        MOUNT_DEVICE_ID, MOUNT_NAME, MOUNT_FIRMWARE_VERSION);

    for (int rate = 1; rate <= 4; rate++) {
        proto_buf_append(out, cap, &len, "%s%.1f",
                         rate > 1 ? "," : "", motors_get_slewing_speed(rate));
    }

    proto_buf_append(out, cap, &len,
        "],\"tracking_modes\":[\"none\",\"sidereal\",\"lunar\",\"solar\"],"
        "\"caps\":{\"guide\":true,\"home\":true,\"park\":true,\"unpark\":true,"
        "\"move_axis\":true,\"slew\":true,\"tracking\":true,\"sync\":false,"
        "\"meridian_flip\":false}"
        "}}");
}
