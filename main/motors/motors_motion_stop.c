/* Motors - motors_motion_stop.c
 *
 * Purpose: emergency stop from outside the motion task — abort the active
 * motion loop immediately, marking the position uncertain (an in-flight RMT
 * batch may have emitted uncounted steps).
 */
#include "motors_internal.h"

#include "freertos/task.h"

void motors_motion_stop(void) {
    /* Bump the epoch FIRST so any command enqueued before this stop — and
     * not yet consumed by the queue — is discarded at execution. */
    motors_stop_generation++;

    s_motion.active = false;
    s_motion.aborted = true;

    /* Clear any in-flight guide pulse so a stale deadline/offset cannot
     * "resume" the pulse after a quick stop → restart. */
    s_motion.ra_guide_deadline_us = 0;
    s_motion.ra_guide_offset_dps = 0.0f;
    s_motion.dec_guide_deadline_us = 0;
    s_motion.dec_guide_offset_dps = 0.0f;

    if (motors_motion_task_handle) {
        xTaskNotify(motors_motion_task_handle, 0, eNoAction);
    }
}
