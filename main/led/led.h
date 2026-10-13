#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * LED states for the external indicator on GPIO 1.
 *
 * NORMAL     — dim brightness (~24 %), mount idle.
 * SLEWING    — full brightness (100 %), mount in motion.
 * ERROR      — beacon (blink-blink, slow fade-out), fatal fault:
 *              motor hardware error or unpowered motors.
 *
 * All state decisions are made inside led_update(), which is the
 * single public entry point for LED state changes. It inspects motor
 * status to pick the right state. No other module
 * should call led_set_state() directly.
 */
typedef enum {
    LED_STATE_NORMAL,
    LED_STATE_SLEWING,
    LED_STATE_ERROR
} LedState;

/* Initialise LEDC PWM on GPIO 1 and start in NORMAL (dim). */
void led_init(void);

/*
 * Periodic update, call every ~50 ms from the runtime loop.
 *
 * Inspects the motors and transitions the LED to
 * the correct state.  This is the ONLY public function that changes LED
 * state — there is no separate set_state / clear_error API.
 */
void led_update(void);
