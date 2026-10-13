#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Binary semaphore posted by the TinyUSB CDC_EVENT_RX callback; the RX task
 * blocks on it and then drains the CDC RX FIFO. */
extern SemaphoreHandle_t usb_cdc_rx_sem;

/* Mutex serialising usb_cdc_send() between the protocol RX task and the
 * telemetry task. */
extern SemaphoreHandle_t usb_cdc_tx_mutex;

/* Start the dedicated RX task (called once by usb_cdc_init). */
void usb_cdc_rx_task_start(void);

/* Clear any partially-queued TX frame and re-arm sending.  Called on host
 * reconnect (DTR assert) so a frame abandoned mid-send never leaks its tail
 * into the new session's byte stream. */
void usb_cdc_tx_reset(void);
