/* LED — led_beacon.c
 *
 * Purpose: beacon animation for the "no external power" state.
 *
 * Two quick blinks, then a slow 1 s fade-out — like a lighthouse — so an
 * unpowered mount is obvious at a glance.  Driven by a one-shot esp_timer
 * that re-arms itself for each phase.
 */
#include "led_internal.h"

#include "esp_timer.h"

/* One step of the beacon cycle. */
typedef struct {
    uint32_t duty;      /* target duty for this step (13-bit) */
    uint32_t hold_ms;   /* how long this step lasts */
    uint32_t fade_ms;   /* fade duration to reach `duty`; 0 = instant */
} BeaconStep;

/* blink -> blink -> slow 1 s fade-out -> repeat */
static const BeaconStep s_beacon_steps[] = {
    { LED_BRIGHT_DUTY, 200, 0               },  /* blink 1 on  */
    { 0,               100, 0               },  /* blink 1 off */
    { LED_BRIGHT_DUTY, 200, 0               },  /* blink 2 on  */
    { 0,               1000,  800  },  /* slow fade out */
};
#define BEACON_STEP_COUNT (sizeof(s_beacon_steps) / sizeof(s_beacon_steps[0]))

static esp_timer_handle_t s_beacon_timer;
static unsigned int       s_beacon_phase;

static void beacon_tick(void *arg) {
    (void) arg;

    const BeaconStep *step = &s_beacon_steps[s_beacon_phase];

    if (step->fade_ms > 0) {
        ledc_set_fade_time_and_start(LED_MODE, LED_CHANNEL, step->duty,
                                     step->fade_ms, LEDC_FADE_NO_WAIT);
    } else {
        ledc_set_duty(LED_MODE, LED_CHANNEL, step->duty);
        ledc_update_duty(LED_MODE, LED_CHANNEL);
    }

    s_beacon_phase = (s_beacon_phase + 1) % BEACON_STEP_COUNT;

    esp_timer_start_once(s_beacon_timer, (uint64_t) step->hold_ms * 1000UL);
}

void led_beacon_start(void) {
    if (s_beacon_timer) {
        /* Already running — nothing to do. */
        return;
    }

    s_beacon_phase = 0;

    esp_timer_create_args_t args = {
        .callback = beacon_tick,
        .arg      = NULL,
        .name     = "led_beacon",
    };
    esp_timer_create(&args, &s_beacon_timer);

    /* Apply the first step immediately and schedule the next. */
    beacon_tick(NULL);
}

void led_beacon_stop(void) {
    if (!s_beacon_timer) {
        return;
    }

    esp_timer_stop(s_beacon_timer);
    esp_timer_delete(s_beacon_timer);
    s_beacon_timer = NULL;
}
