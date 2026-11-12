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

/* Status LED — onboard WS2812 RGB, driven via RMT (green = channel G). */
#define LED_GPIO        GPIO_NUM_48

/* Active event buzzer — small module, I/O pin driven from the GPIO (HIGH = beep). */
#define BUZZER_GPIO     GPIO_NUM_1

/* =========================================================================
 * Motor resolution & mechanics.
 * ========================================================================= */

/* Microstep resolution — TMC2209 CHOPCONF.MRES (MRES=2).  BFT: 64 microsteps. */
#define MOTORS_MICROSTEPS 64

/* TMC2209 coil current (0–31 fields, IRUN/IHOLD).  Provisional values —
 * tune on the bench for the NEMA 17; see TMC.md. */
#define TMC_IRUN  14
#define TMC_IHOLD 10

/* TMC2209 StealthChop → SpreadCycle transition threshold (TPWMTHRS, 0–1048575).
 * Below the threshold the motors run silent StealthChop (covers tracking and
 * guiding); above it they switch to SpreadCycle, which keeps torque and stays
 * smoother at high speed (slewing).  0 = always StealthChop (silent but weak at
 * high speed).  At 64 µsteps, 250 ≈ 0.68°/s axis; lower value = higher threshold. */
#define TMC_TPWMTHRS 64

/* Total gear reduction: 5:1 belt (20T→100T GT2 9mm) × 100:1 harmonic drive.
 * motor shaft turns : axis turns. */
#define TOTAL_GEAR_REDUCTION (500.0f)
