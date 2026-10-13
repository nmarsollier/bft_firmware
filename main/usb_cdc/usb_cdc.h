#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * USB CDC-ACM transport for the mount protocol.
 *
 * Configures the ESP32-S3 USB OTG peripheral as a single CDC-ACM serial port
 * via TinyUSB.  The host sees a plain serial port; the mount protocol is
 * carried as length-prefixed JSON frames over that byte stream.
 */

/* Callback invoked with every chunk of bytes received from the host.
 * Runs in the CDC RX task context, not in a TinyUSB callback. */
typedef void (*usb_cdc_rx_fn)(const uint8_t *data, size_t len, void *arg);

/*
 * Install the TinyUSB CDC-ACM device and start the RX task.
 * Returns ESP_OK on success; the device enumerates as a serial port once the
 * host re-enumerates.
 */
esp_err_t usb_cdc_init(void);

/* Register the receive callback invoked by the RX task. */
void usb_cdc_set_rx_callback(usb_cdc_rx_fn cb, void *arg);

/* Callback invoked when the host asserts (connected) or deasserts
 * (disconnected) DTR — i.e. opens or closes the serial port. */
typedef void (*usb_cdc_conn_fn)(bool connected, void *arg);

/* Register a callback invoked on host connect/disconnect (DTR change). */
void usb_cdc_set_conn_callback(usb_cdc_conn_fn cb, void *arg);

/*
 * Queue `data` for transmission and flush to the host.  Non-blocking: returns
 * false when the TX FIFO cannot accept the whole buffer (the caller may drop
 * the frame — telemetry is replaceable, command responses are not retried).
 */
bool usb_cdc_send(const uint8_t *data, size_t len);
