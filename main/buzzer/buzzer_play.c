/* Buzzer — buzzer_play.c
 *
 * Purpose: non-blocking beep patterns for the event buzzer.
 *
 * Patterns are fixed tables of steps (tone on/off + duration).  A
 * one-shot esp_timer walks the steps, driving the LEDC duty to
 * BUZZER_TONE_DUTY (2 kHz tone) or BUZZER_OFF_DUTY (silence), so the
 * runtime loop is never blocked.  A step with duration_ms == 0 marks
 * the end of the pattern.
 */
#include "buzzer_internal.h"

#include "esp_log.h"
#include "esp_timer.h"

/* ── Pattern tables (sentinel = {0, false}) ────────────────── */

static const BuzzerStep boot_steps[] = {
    { 100, true },
    { 0,  false },
};

static const BuzzerStep motion_start_steps[] = {
    { 150, true },
    { 0,  false },
};

static const BuzzerStep motion_end_steps[] = {
    { 150, true },
    { 0,  false },
};

/* ── Playback state ────────────────────────────────────────── */

static esp_timer_handle_t s_timer;
static const BuzzerStep *s_steps;
static uint32_t          s_index;

static void set_duty(uint32_t duty) {
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, duty);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

static void apply_step(void) {
    const BuzzerStep *step = &s_steps[s_index];

    if (step->duration_ms == 0) {
        set_duty(BUZZER_OFF_DUTY);
        return;  /* pattern finished — timer left idle */
    }

    set_duty(step->tone_on ? BUZZER_TONE_DUTY : BUZZER_OFF_DUTY);
    esp_timer_start_once(s_timer, step->duration_ms * 1000ULL);
}

static void step_tick(void *arg) {
    (void) arg;
    s_index++;
    apply_step();
}

void buzzer_play(BuzzerPattern pattern) {
    if (s_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = step_tick,
            .arg      = NULL,
            .name     = "buzzer_beep",
        };
        esp_timer_create(&args, &s_timer);
    } else {
        /* Best-effort cancel of any in-flight pattern (harmless no-op
         * error if the timer already finished and went idle). */
        esp_timer_stop(s_timer);
    }

    switch (pattern) {
    case BUZZER_PATTERN_BOOT:
        s_steps = boot_steps;
        break;
    case BUZZER_PATTERN_MOTION_START:
        s_steps = motion_start_steps;
        break;
    case BUZZER_PATTERN_MOTION_END:
        s_steps = motion_end_steps;
        break;
    }

    s_index = 0;
    apply_step();
}
