/* Mount - mount_home.c
 *
 * Purpose: send the mount to its home position.
 */
#include "mount.h"
#include "mount_internal.h"

#include "motors.h"

MountResult mount_home(void) {
    if (mount_is_error()) {
        return mount_result_error_state();
    }

    /* HOME is the recovery mechanism: it moves to the pole (0,0), which is
     * always above the horizon.  Do NOT gate it on the current path — a mount
     * that ended up below the horizon must still be able to return home, the
     * same way manual MoveAxis recovery is always allowed (MOUNT.md §11). */
    MotorResultCode rc = motors_home();
    return motors_result_code_error_result(rc);
}
