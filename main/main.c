/* Main - main.c
 *
 * Purpose: start the runtime subsystems, then the serial protocol (CDC),
 * and the main loop.
 */
#include "esp_log.h"
#include "runtime.h"

#if CONFIG_TINYUSB_CDC_ENABLED
#include "proto.h"
#endif

static const char *TAG = "MAIN";

void app_main(void) {
    setup_init();

#if CONFIG_TINYUSB_CDC_ENABLED
    proto_init();
#endif

    setup_runtime_start();

    ESP_LOGI(TAG, "Started");
}
