/* LED — led_update.c
 *
 * Purpose: periodic LED state decision — the single public entry point
 * for all LED state changes.
 *
 * Called from the runtime loop every ~50 ms.  Inspects the motors status
 * to pick the correct LED state:
 *
 *   1. Fatal error   -> beacon (blink-blink-fade-pause)
 *   2. Motor SLEWING -> full brightness
 *   3. Otherwise     -> dim (normal idle)
 *
 * No other module calls led_set_state() directly.  This keeps all LED
 * logic cohesive in one place and prevents scattered, conflicting calls.
 */

#include "led_internal.h"

#include "motors/motors.h"

void led_update(void) {
    MotorsState ms = motors_current_state();

    /*
     * 1. Fatal conditions override everything and beacon in ERROR: a motor
     *    hardware fault or unpowered motors.
     */
    if (motors_status_is_error(ms.status)) {
        led_set_state(LED_STATE_ERROR);
        return;
    }

    /*
     * 2. Normal operation — bright while slewing, dim otherwise.
     */
    if (ms.status == MOTORS_STATUS_SLEWING) {
        led_set_state(LED_STATE_SLEWING);
    } else {
        led_set_state(LED_STATE_NORMAL);
    }
}
