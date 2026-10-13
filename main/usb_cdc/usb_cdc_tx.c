/* USB CDC — usb_cdc_tx.c — frame transmission to the host. */
#include "usb_cdc.h"
#include "usb_cdc_internal.h"

#include "tinyusb_cdc_acm.h"
#include "class/cdc/cdc_device.h"   /* tud_cdc_n_write_clear */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Give up queueing a frame if the host stops draining the CDC ring for this
 * long.  A command response is small and sent rarely, so this only trips when
 * the host has gone away. */
#define USB_CDC_TX_TIMEOUT_MS 1000

/* Set when a frame was only partially queued (the host stopped draining before
 * the whole frame got into the CDC ring).  The stream is then desynchronised:
 * the next frame would be read as the tail of the incomplete one, so further
 * sends are refused until the host reconnects and usb_cdc_tx_reset() clears
 * both the flag and the residual bytes in the FIFO. */
static bool s_tx_failed = false;

void usb_cdc_tx_reset(void) {
    if (usb_cdc_tx_mutex == NULL) {
        return;
    }

    /* Serialise with usb_cdc_send(): clearing the FIFO mid-send would drop the
     * front of a frame and leave its tail queued, reproducing the same
     * half-frame corruption this reset is meant to prevent. */
    if (xSemaphoreTake(usb_cdc_tx_mutex, portMAX_DELAY) == pdTRUE) {
        tud_cdc_n_write_clear(TINYUSB_CDC_ACM_0);
        s_tx_failed = false;
        xSemaphoreGive(usb_cdc_tx_mutex);
    }
}

bool usb_cdc_send(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return false;
    }

    if (usb_cdc_tx_mutex == NULL || s_tx_failed) {
        return false;
    }

    if (xSemaphoreTake(usb_cdc_tx_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    /* Queue the whole frame, advancing past whatever the CDC TX ring accepts.
     * When the ring is full, yield so the TinyUSB task drains it to the host.
     * A partially-queued frame must never be left behind: the next frame would
     * be read as the tail of this one and desynchronise the length framing. */
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(USB_CDC_TX_TIMEOUT_MS);
    size_t off = 0;
    while (off < len) {
        size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data + off, len - off);
        off += queued;
        if (off == len) {
            break;
        }
        if (queued == 0 && (TickType_t)(xTaskGetTickCount() - start) >= timeout_ticks) {
            /* Partially queued — the tail of this frame is missing and cannot
             * be retracted.  Abandon the stream and force a host reconnect. */
            s_tx_failed = true;
            break;
        }
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);

    xSemaphoreGive(usb_cdc_tx_mutex);
    return off == len;
}
