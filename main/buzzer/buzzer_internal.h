#pragma once

#include "buzzer.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Active buzzer drive levels.
 *
 * The buzzer is powered from 5V and switched through a UMC2003 Darlington
 * (sinking) output, so GPIO HIGH activates the load and sounds the buzzer;
 * GPIO LOW releases it (silent, no standing current).
 */
#define BUZZER_ACTIVE_LEVEL  1
#define BUZZER_IDLE_LEVEL    0

/* ── Beep patterns ─────────────────────────────────────────── */

typedef enum {
    BUZZER_PATTERN_BOOT,
    BUZZER_PATTERN_MOTION_START,
    BUZZER_PATTERN_MOTION_END,
} BuzzerPattern;

/*
 * One pattern step: sound (or stay silent) for the given duration.
 * A step with duration_ms == 0 marks the end of the pattern.
 */
typedef struct {
    uint32_t duration_ms;
    bool beep_on;
} BuzzerStep;

/* ── Internal API ──────────────────────────────────────────── */

/*
 * Start the given beep pattern (non-blocking).
 * Internal — called only from buzzer_init() and buzzer_update().
 */
void buzzer_play(BuzzerPattern pattern);
