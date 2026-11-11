/* Led — led_beacon.c
 *
 * Purpose: beacon animation for the fault state (unpowered TMC / motor fault).
 *
 * Two quick green blinks, then a pause — like a lighthouse — so a fault is
 * obvious at a glance. Driven by a one-shot esp_timer that re-arms itself.
 */
#include "led_internal.h"

#include "esp_timer.h"

/* blink -> blink -> pause -> repeat */
static const uint32_t s_beacon_hold_ms[] = { 200, 100, 200, 900 };
#define BEACON_STEP_COUNT (sizeof(s_beacon_hold_ms) / sizeof(s_beacon_hold_ms[0]))

static esp_timer_handle_t s_beacon_timer;
static unsigned int       s_beacon_phase;

static void beacon_tick(void *arg) {
    (void) arg;

    bool on = (s_beacon_phase == 0 || s_beacon_phase == 2);
    if (on) {
        led_set_color(0, LED_ERROR_G, 0);
    } else {
        led_set_color(0, 0, 0);
    }

    uint32_t hold_ms = s_beacon_hold_ms[s_beacon_phase];
    s_beacon_phase = (s_beacon_phase + 1) % BEACON_STEP_COUNT;

    esp_timer_start_once(s_beacon_timer, (uint64_t) hold_ms * 1000UL);
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
