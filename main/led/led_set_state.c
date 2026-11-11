/* Led — led_set_state.c
 *
 * Purpose: apply LED state transitions (dim green / bright green / beacon).
 *
 * Internal helper called exclusively from led_update(); other modules must
 * never call led_set_state() directly.
 */
#include "led_internal.h"

#include "esp_err.h"
#include "esp_log.h"

/* ── Shared state ──────────────────────────────────────────── */

LedState led_current_state = LED_STATE_NORMAL;

static const char *TAG = "LED";

/* Persistent transmit buffer: rmt_transmit() may encode the data after the call
 * returns, so the buffer must outlive led_set_color() — a stack array would be
 * dead by then. */
static uint8_t s_rgb[3];

/* Set when a transmission did not finish within the wait window.  While set,
 * s_rgb may still be in use by RMT, so no further transmission is started until
 * the channel reports completion. */
static bool s_pending;

/* ── Internal API ──────────────────────────────────────────── */

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    /* Never transmit unsynchronised: if the mutex failed to initialise there is
     * no way to guarantee s_rgb is not overwritten mid-transmission. */
    if (led_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(led_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    /* If the previous transmission is still pending, drop this update rather
     * than overwrite s_rgb while RMT is still reading it. */
    if (s_pending) {
        if (rmt_tx_wait_all_done(led_tx_chan, 0) != ESP_OK) {
            xSemaphoreGive(led_mutex);
            return;
        }
        s_pending = false;
    }

    /* YD-ESP32-S3 onboard WS2812 is GRB order (green first); swap to [r,g,b]
     * if this clone is RGB (green would then show as red). */
    s_rgb[0] = g;
    s_rgb[1] = r;
    s_rgb[2] = b;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(led_tx_chan, led_encoder, s_rgb, sizeof(s_rgb), &tx_conf);
    if (err == ESP_OK) {
        /* A WS2812 frame is ~30 µs, so 100 ms is effectively "until done". */
        err = rmt_tx_wait_all_done(led_tx_chan, 100);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "led wait incomplete: %s", esp_err_to_name(err));
            /* The transmission is still pending — protect s_rgb from reuse. */
            s_pending = true;
        }
    } else {
        ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    }

    xSemaphoreGive(led_mutex);
}

void led_set_state(LedState state) {
    if (state == led_current_state) {
        return;
    }

    led_current_state = state;

    /* Stop the animation (if any) before applying the new state. */
    led_beacon_stop();

    switch (state) {
    case LED_STATE_NORMAL:
        led_set_color(0, LED_NORMAL_G, 0);
        break;
    case LED_STATE_SLEWING:
        led_set_color(0, LED_SLEWING_G, 0);
        break;
    case LED_STATE_ERROR:
        led_beacon_start();
        break;
    }
}
