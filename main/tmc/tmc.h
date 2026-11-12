/*
 * tmc.h — TMC2209 driver public API (two axes: RA and DEC).
 *
 * The microstep configuration (MOTORS_MICROSTEPS_TRACKING) and the coil currents
 * (TMC_IRUN / TMC_IHOLD) live in config.h.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/*
 * Initialize the two single-wire UARTs, configure GCONF/IHOLD_IRUN/CHOPCONF
 * on both drivers and verify that each latched the requested microsteps.
 *
 * The drivers are always enabled (no EN pin is connected).  Must be called
 * before any step/direction motion is started.
 */
esp_err_t tmc2209_hw_init(void);

/*
 * Probe both drivers over UART: return true only when both answer a register
 * read.  This doubles as the "TMC working" sense — the drivers are fed by VM
 * (12 V), so they are silent when the rail is absent and their logic supply
 * collapses.  Both motors must be alive for the mount to move.
 */
bool tmc2209_probe(void);

/*
 * Re-configure the driver registers after they stop answering and then answer
 * again (power returned): their registers were lost while unpowered, so they
 * are rewritten.  Does not re-install the UARTs.
 */
esp_err_t tmc2209_reinit(void);

/*
 * Change the microstep resolution on both drivers at runtime: read-modify-write
 * of CHOPCONF.MRES (preserving the other fields), verified by read-back with
 * retries.  Returns ESP_OK only when BOTH drivers latched the requested value.
 * Must be called with the mount otherwise idle (no STEP stream in flight).
 */
esp_err_t tmc2209_set_microsteps(uint16_t microsteps);

/*
 * Current active microstep resolution on both drivers.  The value is only ever
 * recorded after a verified read-back, so it is always the real one — the motors
 * layer trusts it for step accounting and status reporting.
 */
uint16_t tmc2209_get_microsteps(void);

/*
 * Query whether both TMC2209 UARTs and drivers were initialised successfully.
 * Used for error-state LED signalling.
 */
bool tmc2209_is_initialized(void);

/* --------------------------------------------------------------------------
 * Init diagnostics — status and failure reason.
 * -------------------------------------------------------------------------- */

/* Init status. */
typedef enum {
    TMC_NOT_INIT = 0,   /* init not run / not reached */
    TMC_OK,             /* configured and verified */
    TMC_ERROR,          /* failed — see tmc2209_get_error() */
} TmcStatus;

/* Failure reason (valid when status == TMC_ERROR). */
typedef enum {
    TMC_ERROR_NONE = 0,
    TMC_ERROR_UART,            /* uart_driver_install / param_config / set_pin */
    TMC_ERROR_GCONF_WRITE,     /* GCONF write + readback */
    TMC_ERROR_IHOLD_WRITE,     /* IHOLD_IRUN write (IFCNT verify) */
    TMC_ERROR_CHOPCONF_VERIFY, /* CHOPCONF write + readback / MRES mismatch */
    TMC_ERROR_TPWMTHRS_WRITE,  /* TPWMTHRS write (IFCNT verify) */
    TMC_ERROR_PWMCONF_VERIFY,  /* PWMCONF write + readback */
    TMC_ERROR_MRES_SET         /* runtime microstep switch: CHOPCONF.MRES write + readback */
} TmcError;

TmcStatus tmc2209_get_status(void);
TmcError  tmc2209_get_error(void);

const char *tmc2209_status_to_string(TmcStatus status);
const char *tmc2209_error_to_string(TmcError error);
