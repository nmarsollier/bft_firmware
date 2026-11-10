/* Mount - mount_settings_update.c
 *
 * Purpose: validate and apply mount settings.
 */
#include "mount.h"
#include "mount_internal.h"

#include <math.h>

MountResult mount_settings_validate(MountSettings settings) {
    if (!isfinite(settings.lat) || settings.lat < -90.0f || settings.lat > 90.0f) {
        return mount_result_error("Latitude is outside valid range -90 <= lat <= 90");
    }

    if (!isfinite(settings.lon) || settings.lon < -180.0f || settings.lon > 180.0f) {
        return mount_result_error("Longitude is outside valid range -180 <= lon <= 180");
    }

    return mount_result_ok();
}

MountResult mount_settings_update(MountSettings settings) {
    MountResult vr = mount_settings_validate(settings);
    if (!vr.ok) {
        return vr;
    }

    /*
     * The site latitude must remain fixed while movement is active or pending
     * (MOUNT.md §10): changing it mid-slew (and especially crossing a
     * hemisphere) changes the reference pole and the astronomical transform.
     * Longitude is not gated the same way.
     */
    if (fabsf(settings.lat - mount_internal_state.lat) > 0.0001f
        && !motors_is_quiescent()) {
        return mount_result_error("Cannot change latitude while the mount is moving");
    }

    mount_internal_state = settings;

    /* Persist the updated settings. */
    mount_settings_save(&mount_internal_state);

    return mount_result_ok();
}
