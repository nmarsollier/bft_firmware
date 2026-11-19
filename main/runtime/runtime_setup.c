/* Runtime - runtime_setup.c
 *
 * Purpose: initialize the subsystems needed before the runtime loop starts.
 */
#include "runtime.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "buzzer.h"
#include "led.h"
#include "motors.h"
#include "mount.h"
#include "tmc.h"
#if CONFIG_TINYUSB_CDC_ENABLED
#include "usb_cdc.h"
#endif

static const char *TAG = "RUNTIME_SETUP";

/*
 * Business use case: prepare the mount for operation.
 *
 * Objective: bring the transports, core services, and peripherals online so the
 * mount starts in a usable state.
 *
 * LED state is managed exclusively by led_update() in the runtime loop —
 * no explicit led_set_state() calls are needed here.  The loop picks up
 * motor status on its first tick.
 */
void setup_init(void) {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    led_init();
    buzzer_init();

#if CONFIG_TINYUSB_CDC_ENABLED
    /* USB CDC-ACM serial transport — non-fatal, mount works without USB. */
    esp_err_t cdc_result = usb_cdc_init();
    if (cdc_result != ESP_OK) {
        ESP_LOGW(TAG, "USB CDC init skipped: %s", esp_err_to_name(cdc_result));
    }
#endif

    mount_init();

    /* Configure the two TMC2209s (UART + registers) before the motors so
     * motors_init() can detect whether the drivers are alive. Logs its own
     * failure. */
    tmc2209_hw_init();

    esp_err_t motors_err = motors_init();
    if (motors_err != ESP_OK) {
        ESP_LOGE(TAG, "motors_init failed: %s — mount in ERROR state, reboot required",
                 esp_err_to_name(motors_err));
    }

    ESP_LOGI(TAG, "Mount ready");
}
