/* Mount - mount_tracking.c
 *
 * Purpose: change the active tracking mode.
 */
#include "mount.h"
#include "mount_internal.h"

#include "motors.h"

/*
 * Business use case: change the mount's active tracking mode.
 *
 * Objective: let clients choose how the mount compensates for the sky's
 * apparent motion during observations while respecting state rules.
 */
MountResult mount_set_tracking(TrackingMode tracking) {
    if (mount_is_error()) {
        return mount_result_error_state();
    }

    MotorResultCode rc = motors_start_tracking(tracking);
    if (rc != MOTOR_OK) {
        return motors_result_code_error_result(rc);
    }

    return mount_result_ok();
}
