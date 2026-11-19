/* Motors - motors_is_slewing.c
 *
 * Purpose: report whether the mount is slewing — the only motion fast enough
 * that a UART read would disturb the RMT step stream.  Used by the runtime
 * power probe to decide when it is safe to read the drivers.
 */
#include "motors.h"

bool motors_is_slewing(void) {
    return motors_current_state().status == MOTORS_STATUS_SLEWING;
}
