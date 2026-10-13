#pragma once

/*
 * Event buzzer (passive, 2 kHz) on GPIO 2, driven directly.
 *
 * Produces short audible beeps for lifecycle events: power-on, and the
 * start / end of a goto or move-axis motion.
 *
 * All event decisions are made inside buzzer_update(), which polls the
 * motors status and fires the matching beep on SLEWING transitions.  No
 * other module should call buzzer_play() directly.
 */

/* Initialise LEDC PWM (2 kHz) on GPIO 2 and emit the power-on beep. */
void buzzer_init(void);

/*
 * Periodic update, call every ~100 ms from the runtime loop.
 *
 * Detects MOTORS_STATUS_SLEWING transitions (motion start / end) and
 * emits the corresponding beep.  This is the ONLY public function that
 * changes the buzzer state.
 */
void buzzer_update(void);
