#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * External-power sensing for the BFT mount.
 *
 * The ESP32-S3 board can be powered from USB alone, in which case the 12V
 * switch is off and the motors have no power — yet the firmware would still
 * run and "move" the mount.  This module senses the 5.5V output of the
 * LM2596 (fed by the 12V rail) through a 10k/10k divider on GPIO 1, so the
 * mount can refuse to move when the motors are unpowered.
 */

/* Initialise the ADC channel that senses the external-power rail. */
esp_err_t power_init(void);

/* True when the external 5.5V rail (12V switch on) is present. */
bool power_has_external(void);
