/* Motors - motors_motion_stop_graceful.c
 *
 * Purpose: graceful stop — request the motion loop to stop submitting new
 * batches but first confirm + account the batches already committed to RMT,
 * keeping the position counters exact.
 */
#include "motors_internal.h"

#include "freertos/task.h"

void motors_motion_stop_graceful(void) {
    /* Bump the epoch FIRST so a command enqueued before this stop is discarded. */
    motors_stop_generation++;

    s_motion.active = false;
    s_motion.graceful = true;

    /* Clear any in-flight guide pulse (same rationale as motors_motion_stop). */
    s_motion.ra_guide_deadline_us = 0;
    s_motion.ra_guide_offset_dps = 0.0f;
    s_motion.dec_guide_deadline_us = 0;
    s_motion.dec_guide_offset_dps = 0.0f;

    if (motors_motion_task_handle) {
        xTaskNotify(motors_motion_task_handle, 0, eNoAction);
    }
}
