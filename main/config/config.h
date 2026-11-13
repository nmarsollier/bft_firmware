#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

/*
 * Mount hardware configuration.
 *
 * Single source of truth for the build-time constants that vary between
 * mounts: pin assignments, motor resolution and mechanical reduction.
 * To adapt the firmware to a different mount, edit this file only —
 * no other module may hardcode these values.
 */

/* =========================================================================
 * Mount identity.
 * ========================================================================= */

/* Mount identity — reported in the unified `capabilities` handshake. */
#define MOUNT_NAME             "BFT"
#define MOUNT_DEVICE_ID        "bft"
#define MOUNT_FIRMWARE_VERSION "0.1.0" /* TODO: bump to the real version */

/* =========================================================================
 * Pin assignments — BFT (ESP32-S3 44-pin board, TMC2209 revision).
 * ========================================================================= */

/* Step / direction pins — direct to the TMC2209 STEP/DIR inputs (3.3 V logic,
 * no level shifting). */
#define DEC_STEP_GPIO  GPIO_NUM_15
#define DEC_DIR_GPIO   GPIO_NUM_7
#define RA_STEP_GPIO   GPIO_NUM_10
#define RA_DIR_GPIO    GPIO_NUM_9

/* TMC2209 drivers — one single-wire UART per axis (TX through a 1 kΩ series
 * resistor, RX direct to the PDN_UART line). */
#define TMC_RA_UART_NUM   UART_NUM_1
#define TMC_RA_TX_GPIO    GPIO_NUM_12
#define TMC_RA_RX_GPIO    GPIO_NUM_13
#define TMC_DEC_UART_NUM  UART_NUM_2
#define TMC_DEC_TX_GPIO   GPIO_NUM_17
#define TMC_DEC_RX_GPIO   GPIO_NUM_18

/* Status LED — external indicator on GPIO 5, driven via LEDC PWM.  The GPIO
 * sources current (3.3 V) into the anode through a series resistor; the
 * cathode is tied direct to GND. */
#define LED_GPIO        GPIO_NUM_5

/* Active event buzzer — small module, I/O pin driven from the GPIO (HIGH = beep). */
#define BUZZER_GPIO     GPIO_NUM_4

/* =========================================================================
 * Motor resolution & mechanics.
 * ========================================================================= */

/* Microstep resolution — TMC2209 CHOPCONF.MRES (MRES=2).  BFT: 64 microsteps.
 * This is the canonical (finest) resolution: the STEP counters are always in
 * these units, and every non-slew motion (tracking, guiding, move-axis) runs
 * at this resolution. */
#define MOTORS_MICROSTEPS_TRACKING 64

/* Coarse microstep resolution for large ramped slews (CHOPCONF.MRES=5).
 * 8 µsteps cuts the STEP pulse count 8× versus 64, relaxing the RMT timing at
 * high slew speeds; with INTPOL the driver still interpolates to 256 µsteps,
 * so mechanical smoothness is unchanged. */
#define MOTORS_MICROSTEPS_SLEW 8

/* Gentle-slew boundary (degrees): a slew whose larger axis travel stays below
 * this is treated gently — fine microsteps (MOTORS_MICROSTEPS_TRACKING) and a
 * capped speed; above it the slew runs coarse (MOTORS_MICROSTEPS_SLEW) at full
 * speed.  Single source for both the microstep switch and the ramp speed cap. */
#define MOTORS_SLEW_GENTLE_DEG 8.0f

/* TMC2209 coil current (0–31 fields, IRUN/IHOLD).  Provisional values —
 * tune on the bench for the NEMA 17; see TMC.md. */
#define TMC_IRUN  14
#define TMC_IHOLD 10

/* TMC2209 StealthChop → SpreadCycle transition threshold (TPWMTHRS, 0–1048575).
 * Below the threshold the motors run silent StealthChop (covers tracking and
 * guiding); above it they switch to SpreadCycle, which keeps torque and stays
 * smoother at high speed (slewing).  0 = always StealthChop (silent but weak at
 * high speed).  TPWMTHRS=64 transitions at ~2.6°/s axis — just above where
 * StealthChop stops regulating well.  Lower value = higher transition speed.
 * The transition speed is independent of the microstep resolution (same at
 * 8 or 64 µsteps, since f_STEP ∝ µ for a fixed axis speed). */
#define TMC_TPWMTHRS 64

/* Total gear reduction: 5:1 belt (20T→100T GT2 9mm) × 100:1 harmonic drive.
 * motor shaft turns : axis turns. */
#define TOTAL_GEAR_REDUCTION (500.0f)
