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

    MotorResultCode rc = motors_home();
    return motors_result_code_error_result(rc);
}
