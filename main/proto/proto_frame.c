/* Proto — proto_frame.c — length-prefixed JSON framing and resynchronisation.
 *
 * A frame is a 4-byte little-endian length followed by that many bytes of
 * UTF-8 JSON.  USB CDC-ACM is reliable (CRC + retransmission at the transport
 * layer), so there is no application CRC; the only corruption source left is a
 * partial read or a version mismatch, which the length sanity check plus the
 * '{' … '}' brace check recover from by dropping one byte and rescanning.
 *
 * Frames are drained incrementally as bytes arrive so a large frame followed by
 * the start of another never overflows the buffer and loses the first one.
 */
#include "proto_internal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "usb_cdc.h"

static const char *TAG = "PROTO_FRAME";

/* Drop a partial frame that fails to complete within this window.  A truncated
 * length prefix with a plausible value would otherwise leave the parser waiting
 * for bytes that never arrive. */
#define PROTO_RX_STUCK_US 1000000

/* Static RX buffer (no heap): a full frame plus its length prefix. */
static uint8_t s_rx[PROTO_MAX_FRAME + 4];
static size_t s_rx_len;
static int64_t s_rx_progress_us;   /* last time the buffer made progress */

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t) p[0] |
           ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void dispatch_body(const uint8_t *body, size_t len) {
    char buf[PROTO_MAX_FRAME + 1];
    if (len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, body, len);
    buf[len] = '\0';
    proto_dispatch(buf);
}

/* Drain every complete frame currently held in s_rx, front to back. */
static void proto_drain(void) {
    while (s_rx_len >= 4) {
        uint32_t body_len = read_le32(s_rx);
        if (body_len == 0 || body_len > PROTO_MAX_FRAME) {
            /* Not a plausible length — drop a byte and rescan. */
            memmove(s_rx, s_rx + 1, s_rx_len - 1);
            s_rx_len--;
            continue;
        }

        if (s_rx_len < 4 + body_len) {
            break;   /* frame incomplete, wait for more bytes */
        }

        if (s_rx[4] == '{' && s_rx[4 + body_len - 1] == '}') {
            dispatch_body(&s_rx[4], body_len);
        }

        size_t total = 4 + body_len;
        memmove(s_rx, s_rx + total, s_rx_len - total);
        s_rx_len -= total;
    }
}

void proto_feed(const uint8_t *data, size_t len) {
    /* A partial frame that has been idle past the timeout is stuck (a bogus
     * length) — drop it so the next bytes can resynchronise. */
    if (s_rx_len > 0 && (esp_timer_get_time() - s_rx_progress_us) > PROTO_RX_STUCK_US) {
        s_rx_len = 0;
    }

    for (size_t i = 0; i < len; i++) {
        if (s_rx_len >= sizeof(s_rx)) {
            /* Full but no complete frame drained — drop a byte and rescan. */
            memmove(s_rx, s_rx + 1, s_rx_len - 1);
            s_rx_len--;
        }
        s_rx[s_rx_len++] = data[i];
        s_rx_progress_us = esp_timer_get_time();
        proto_drain();
    }
}

/* Drop any half-received frame.  Called on host reconnect so a frame split
 * across the old and new sessions cannot be completed by the new bytes. */
void proto_frame_reset(void) {
    s_rx_len = 0;
}

void proto_send_body(const char *body) {
    size_t len = strlen(body);
    if (len == 0 || len > PROTO_MAX_FRAME) {
        ESP_LOGW(TAG, "refusing to send %u-byte body", (unsigned) len);
        return;
    }

    uint8_t frame[PROTO_MAX_FRAME + 4];
    frame[0] = (uint8_t) (len & 0xFF);
    frame[1] = (uint8_t) ((len >> 8) & 0xFF);
    frame[2] = (uint8_t) ((len >> 16) & 0xFF);
    frame[3] = (uint8_t) ((len >> 24) & 0xFF);
    memcpy(frame + 4, body, len);

    if (!usb_cdc_send(frame, 4 + len)) {
        ESP_LOGW(TAG, "failed to queue %u-byte frame", (unsigned) (4 + len));
    }
}
