#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Status LED — the onboard WS2812 RGB indicator (GPIO 48), shown green.
 *
 * NORMAL   — dim green, mount idle.
 * SLEWING  — bright green, mount in motion.
 * ERROR    — green beacon (blink-blink-pause), fatal fault: motor hardware
 *            error or unpowered TMC drivers.
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

/* Initialise the WS2812 (RMT) driver and start in NORMAL (dim green). */
void led_init(void);

/*
 * Periodic update, call every ~200 ms from the runtime loop.
 *
 * Inspects the motors and transitions the LED to
 * the correct state.  This is the ONLY public function that changes LED
 * state — there is no separate set_state / clear_error API.
 */
void led_update(void);
