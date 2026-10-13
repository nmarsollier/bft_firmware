/* Motors - motors_hw.c
 *
 * Purpose: DIR GPIO control for the open-loop stepper
 * drivers (TB6600, NRM-BFM).
 *
 * STEP pulse generation is handled by the RMT peripheral (motors_rmt.c)
 * with hardware-timed pulse shapes (RA via DMA, DEC via FIFO).
 * ENABLE is hardwired (always enabled) — no GPIO control needed.
 *
 * Hardware: NEMA 17 open-loop stepper motors with TB6600 drivers,
 * 32-microstep DIP-switch setting, 500:1 total reduction.
 *
 * Level shifting: the STEP/DIR signals (both axes) go through a UMC2003
 * Darlington array (open-collector sinking output).  GPIO HIGH (3.3 V)
 * turns the channel ON, pulling the driver's optocoupler cathode to GND.
 * The optocoupler anode connects to the driver control rail (+V).
 * This inverts the signal at the driver side.
 */

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "motors_internal.h"

/* DIR setup time before the STEP pulse — conservative value, safe for the
 * TB6600 driver. */
#define DIR_SETUP_TIME_US 50

/* Cached last directions to avoid redundant GPIO writes. */
static int last_dir_ra = -1;
static int last_dir_dec = -1;

esp_err_t motors_hw_init(void) {
    /*
     * DIR pins are managed via GPIO.
     * STEP pins (GPIO 14, GPIO 12) are owned by the RMT peripheral and
     * configured by motors_rmt_init().
     */
    const uint64_t pin_mask =
            (1ULL << RA_DIR_GPIO) |
            (1ULL << DEC_DIR_GPIO);

    gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&config);

    if (result != ESP_OK) {
        return result;
    }

    last_dir_ra = 0;
    last_dir_dec = 0;

    return ESP_OK;
}

void motors_hw_set_direction_ra(MotorDirection direction) {
    /* With UMC2003: GPIO HIGH → output sinks → driver sees LOW.
     * RA positive rotation maps to GPIO 0 → driver DIR- = 1. */
    int dir = direction == MOTOR_DIRECTION_POSITIVE ? 0 : 1;
    if (last_dir_ra != dir) {
        last_dir_ra = dir;
        gpio_set_level(RA_DIR_GPIO, dir);
        esp_rom_delay_us(DIR_SETUP_TIME_US);
    }
}

void motors_hw_set_direction_dec(MotorDirection direction) {
    /* With UMC2003: GPIO HIGH → output sinks → driver sees LOW.
     * DEC positive rotation maps to GPIO 1 → driver DIR- = 0. */
    int dir = direction == MOTOR_DIRECTION_POSITIVE ? 1 : 0;
    if (last_dir_dec != dir) {
        last_dir_dec = dir;
        gpio_set_level(DEC_DIR_GPIO, dir);
        esp_rom_delay_us(DIR_SETUP_TIME_US);
    }
}
