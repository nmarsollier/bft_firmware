/* TMC - tmc_get_initialized.c
 *
 * Purpose: track whether both TMC2209 drivers were initialised, for
 * tmc2209_is_initialized() (LED error signalling).
 *
 * Set by tmc_driver.c (tmc_configure_drivers) once both drivers are configured
 * and verified.
 */

#include "tmc.h"
#include "tmc_internal.h"

static bool s_initialized = false;

void tmc2209_set_initialized(bool initialized) {
    s_initialized = initialized;
}

bool tmc2209_is_initialized(void) {
    return s_initialized;
}
