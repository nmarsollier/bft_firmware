/* TMC - tmc_status.c
 *
 * Purpose: init diagnostics — status and failure reason.
 *
 * tmc_driver.c writes tmc_status / tmc_error as it configures the drivers. The
 * public getters and string helpers here let upper layers report whether the
 * drivers failed and why.
 */
#include "tmc.h"
#include "tmc_internal.h"

TmcStatus tmc_status;
TmcError  tmc_error;

TmcStatus tmc2209_get_status(void) {
    return tmc_status;
}

TmcError tmc2209_get_error(void) {
    return tmc_error;
}

const char *tmc2209_status_to_string(TmcStatus status) {
    switch (status) {
        case TMC_OK:
            return "ok";
        case TMC_ERROR:
            return "error";
        case TMC_NOT_INIT:
        default:
            return "not_init";
    }
}

const char *tmc2209_error_to_string(TmcError error) {
    switch (error) {
        case TMC_ERROR_UART:
            return "uart";
        case TMC_ERROR_GCONF_WRITE:
            return "gconf_write";
        case TMC_ERROR_IHOLD_WRITE:
            return "ihold_write";
        case TMC_ERROR_CHOPCONF_VERIFY:
            return "chopconf_verify";
        case TMC_ERROR_TPWMTHRS_WRITE:
            return "tpwmthrs_write";
        case TMC_ERROR_PWMCONF_VERIFY:
            return "pwmconf_verify";
        case TMC_ERROR_MRES_SET:
            return "mres_set";
        case TMC_ERROR_NONE:
        default:
            return "none";
    }
}
