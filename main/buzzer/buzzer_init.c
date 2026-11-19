/* Buzzer — buzzer_init.c
 *
 * Purpose: initialise the GPIO for the active event buzzer.
 *
 * Configures the buzzer pin as a push-pull output, leaves it idle (silent),
 * and emits the power-on beep.
 *
 * The buzzer is a small active-buzzer module driven from the GPIO, so the
 * idle level is LOW (silent) and the pin is pulsed HIGH to beep.
 */
#include "buzzer_internal.h"

#include "driver/gpio.h"
#include "esp_err.h"

void buzzer_init(void) {
    gpio_config_t conf = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&conf));
    gpio_set_level(BUZZER_GPIO, BUZZER_IDLE_LEVEL);

    buzzer_play(BUZZER_PATTERN_BOOT);
}
