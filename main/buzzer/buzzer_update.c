/* Buzzer — buzzer_update.c
 *
 * Purpose: periodic motion-event detection — the single public entry
 * point for buzzer state changes.
 *
 * Called from the runtime loop every ~100 ms.  Watches the motors
 * status for SLEWING transitions and fires the matching beep:
 *
 *   1. entering SLEWING  -> motion-start beep (goto or move axis)
 *   2. leaving  SLEWING  -> motion-end beep (unless entering ERROR)
 *
 * Goto and move axis both use MOTORS_STATUS_SLEWING, so they share the
 * same start / end beeps.  No other module calls buzzer_play() directly.
 */
#include "buzzer_internal.h"

#include "motors/motors.h"

void buzzer_update(void) {
    static bool last_slewing = false;

    MotorsStatus status = motors_current_state().status;
    bool slewing = (status == MOTORS_STATUS_SLEWING);

    if (slewing && !last_slewing) {
        buzzer_play(BUZZER_PATTERN_MOTION_START);
    } else if (!slewing && last_slewing && !motors_status_is_error(status)) {
        buzzer_play(BUZZER_PATTERN_MOTION_END);
    }

    last_slewing = slewing;
}
