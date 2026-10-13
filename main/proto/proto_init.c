/* Proto — proto_init.c — wire the CDC receive stream to the frame parser. */
#include "proto.h"
#include "proto_internal.h"

#include "usb_cdc.h"

static void proto_rx(const uint8_t *data, size_t len, void *arg) {
    (void) arg;
    proto_feed(data, len);
}

static void proto_on_conn(bool connected, void *arg) {
    (void) arg;
    if (!connected) {
        /* The client closed the serial port: drop idempotency state so a
         * restarted client's id numbering cannot collide with ours. */
        proto_session_reset();
    } else {
        /* Host reconnected: drop any half-received frame so it cannot be
         * completed by bytes belonging to the new session. */
        proto_frame_reset();
    }
}

void proto_init(void) {
    proto_session_init();
    usb_cdc_set_rx_callback(proto_rx, NULL);
    usb_cdc_set_conn_callback(proto_on_conn, NULL);
}
