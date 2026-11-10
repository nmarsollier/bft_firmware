/* Motors - motors_is_quiescent.c
 *
 * Purpose: report whether the mount is fully at rest — no active motion, no
 * guide pulse, and no command pending in the queue.
 *
 * The mount layer uses this to reject a site-latitude change while movement is
 * active or pending (MOUNT.md §10: latitude must remain fixed during motion).
 */

#include "motors.h"
#include "motors_internal.h"

#include "freertos/queue.h"

bool motors_is_quiescent(void) {
    MotorsState s = motors_current_state();

    if (s.status == MOTORS_STATUS_SLEWING || s.status == MOTORS_STATUS_TRACKING) {
        return false;
    }
    if (s.guiding) {
        return false;
    }

    /* A queued command (e.g. a tracking resume behind an in-flight slew) is a
     * pending movement; the mount is not yet at rest. */
    if (motion_cmd_queue != NULL) {
        MotionCommand cmd;
        if (xQueuePeek(motion_cmd_queue, &cmd, 0) == pdTRUE) {
            return false;
        }
    }

    return true;
}
