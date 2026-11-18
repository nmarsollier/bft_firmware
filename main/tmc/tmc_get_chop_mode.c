/* TMC — tmc_get_chop_mode.c
 *
 * Purpose: report the active chopper mode on both drivers.  The value is only
 * ever recorded after a verified read-back, so it is always the real hardware
 * value.
 */

#include "tmc.h"
#include "tmc_internal.h"

TmcChopMode tmc2209_get_chop_mode(void)
{
    return tmc_chop_mode;
}
