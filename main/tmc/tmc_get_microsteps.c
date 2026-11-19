/* TMC — tmc_get_microsteps.c
 *
 * Purpose: report the active microstep resolution on both drivers.  The value
 * is only ever recorded after a verified read-back, so it is always the real
 * hardware value.
 */

#include "tmc.h"
#include "tmc_internal.h"

uint16_t tmc2209_get_microsteps(void)
{
    return tmc_microsteps;
}
