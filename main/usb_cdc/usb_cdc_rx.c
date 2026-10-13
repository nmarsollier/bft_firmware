/* USB CDC — usb_cdc_rx.c — RX task + receive callback registration. */
#include "usb_cdc.h"
#include "usb_cdc_internal.h"

#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "tinyusb_cdc_acm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "USB_CDC_RX";

/* The RX task stack runs the whole frame path (drain buffer → proto_dispatch →
 * handler), which nests several ~2 KiB buffers on the stack.  Keep it well above
 * that so a stack overflow cannot corrupt the FreeRTOS scheduler state. */
#define USB_CDC_RX_TASK_STACK_BYTES 8192
#define USB_CDC_RX_TASK_PRIORITY    19   /* below TinyUSB (20), above dispatch work */
#define USB_CDC_RX_TASK_CORE        0

SemaphoreHandle_t usb_cdc_rx_sem;
SemaphoreHandle_t usb_cdc_tx_mutex;

static usb_cdc_rx_fn s_rx_fn;
static void *s_rx_arg;

/* Single RX task — safe to keep this drain buffer off the stack. */
static uint8_t s_rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];

void usb_cdc_set_rx_callback(usb_cdc_rx_fn cb, void *arg) {
    s_rx_fn = cb;
    s_rx_arg = arg;
}

/* Drain the CDC RX FIFO until empty and forward every chunk to the registered
 * callback.  Returns once the FIFO is empty so the task can go back to sleep. */
static void usb_cdc_drain(void) {
    size_t received = 0;

    while (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, s_rx_buf, sizeof(s_rx_buf), &received) == ESP_OK
           && received > 0) {
        if (s_rx_fn != NULL) {
            s_rx_fn(s_rx_buf, received, s_rx_arg);
        }
    }
}

static void usb_cdc_rx_task(void *arg) {
    (void) arg;

    while (true) {
        if (xSemaphoreTake(usb_cdc_rx_sem, portMAX_DELAY) == pdTRUE) {
            usb_cdc_drain();
        }
    }
}

void usb_cdc_rx_task_start(void) {
    if (xTaskCreatePinnedToCore(usb_cdc_rx_task, "usb_cdc_rx",
                                USB_CDC_RX_TASK_STACK_BYTES, NULL,
                                USB_CDC_RX_TASK_PRIORITY, NULL,
                                USB_CDC_RX_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "failed to create RX task");
    }
}
