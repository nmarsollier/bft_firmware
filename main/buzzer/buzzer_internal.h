#pragma once

#include "buzzer.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"

/* ── LEDC hardware configuration ──────────────────────────── */

#define BUZZER_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER       LEDC_TIMER_1
#define BUZZER_CHANNEL     LEDC_CHANNEL_1
#define BUZZER_FREQ_HZ     2000
#define BUZZER_DUTY_RES    LEDC_TIMER_13_BIT
#define BUZZER_DUTY_MAX    8191

/* ── Duty levels (13-bit range: 0 – 8191) ─────────────────── */

#define BUZZER_TONE_DUTY   4096   /* ~50 % — audible 2 kHz square wave */
#define BUZZER_OFF_DUTY    0      /* silence */

/* ── Beep patterns ─────────────────────────────────────────── */

typedef enum {
    BUZZER_PATTERN_BOOT,
    BUZZER_PATTERN_MOTION_START,
    BUZZER_PATTERN_MOTION_END,
} BuzzerPattern;

/*
 * One pattern step: emit tone (or silence) for the given duration.
 * A step with duration_ms == 0 marks the end of the pattern.
 */
typedef struct {
    uint32_t duration_ms;
    bool tone_on;
} BuzzerStep;

/* ── Internal API ──────────────────────────────────────────── */

/*
 * Start the given beep pattern (non-blocking).
 * Internal — called only from buzzer_init() and buzzer_update().
 */
void buzzer_play(BuzzerPattern pattern);
