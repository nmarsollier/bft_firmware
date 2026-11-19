#pragma once

#include "buzzer.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Active buzzer drive levels.
 *
 * The buzzer is a small active-buzzer module whose I/O pin is driven from
 * the GPIO: HIGH sounds it, LOW releases it (silent).
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
