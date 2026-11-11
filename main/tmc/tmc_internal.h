/* tmc_internal.h — internal API shared among TMC module files
 *
 * Functions and variables exposed here are for use ONLY within the
 * main/tmc/ module. No file outside tmc/ should include this header.
 */

#pragma once

#include <stdint.h>

#include "tmc.h"

/*
 * Mark the module initialised after both drivers were configured and
 * verified.  Called by tmc_init.c: tmc_configure_drivers().
 */
void tmc2209_set_initialized(bool initialized);

/*
 * Init diagnostics — written by tmc_init.c as the drivers are configured.
 * Read by tmc_status.c for the public getters.
 */
extern TmcStatus tmc_status;
extern TmcError  tmc_error;
