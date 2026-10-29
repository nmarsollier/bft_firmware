#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

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
 * Pin assignments — BFT (ESP32-S3 44-pin board).
 * ========================================================================= */

/* Step / direction pins (contiguous GPIO 14→11 for clean PCB routing):
 *   GPIO 14: STEP- RA
 *   GPIO 13: DIR- RA
 *   GPIO 12: STEP- DEC
 *   GPIO 11: DIR- DEC
 */
#define RA_STEP_GPIO   GPIO_NUM_14
#define RA_DIR_GPIO    GPIO_NUM_13
#define DEC_STEP_GPIO  GPIO_NUM_12
#define DEC_DIR_GPIO   GPIO_NUM_11

/* Status LED — direct GPIO, LEDC PWM. */
#define LED_GPIO        GPIO_NUM_1

/* Active buzzer — switched through UMC2003 (sinking), GPIO HIGH = beep. */
#define BUZZER_GPIO     GPIO_NUM_9

/* External 5v5 rail detection — GPIO 8 (ADC1_CH7), 10k/10k divider on the
 * LM2596 5.5V output. */
#define POWER_SENSE_ADC_UNIT    ADC_UNIT_1
#define POWER_SENSE_ADC_CHANNEL ADC_CHANNEL_7

/* =========================================================================
 * Motor resolution & mechanics.
 * ========================================================================= */

/* Microstep resolution — TB6600 driver DIP-switch setting (SW1-SW3).
 * BFT: 32 microsteps = 6400 steps/revolution. */
#define MOTORS_MICROSTEPS 32

/* Total gear reduction: 5:1 belt (20T→100T GT2 9mm) × 100:1 harmonic drive.
 * motor shaft turns : axis turns. */
#define TOTAL_GEAR_REDUCTION (500.0f)
