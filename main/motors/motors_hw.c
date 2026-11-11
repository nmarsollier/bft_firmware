/* Motors - motors_hw.c
 *
 * Purpose: DIR GPIO control for the two TMC2209 stepper drivers.
 *
 * STEP pulse generation is handled by the RMT peripheral (motors_rmt.c).
 * The drivers are always enabled (no EN pin is connected), so there is no
 * enable GPIO to manage.
 *
 * Hardware: NEMA 17 stepper motors driven directly by TMC2209 (VCC_IO =
 * 3.3 V, no level shifting).  DIR and STEP connect straight to the driver
 * pins, so GPIO level maps to the driver pin with no inversion.
 */

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "motors_internal.h"

/* DIR setup time before the STEP pulse — conservative value, safe for the
 * TMC2209 (which requires only ~100 ns). */
#define DIR_SETUP_TIME_US 50

/* Cached last directions to avoid redundant GPIO writes. */
static int last_dir_ra = -1;
static int last_dir_dec = -1;

esp_err_t motors_hw_init(void) {
    /*
     * DIR pins are managed via GPIO.  STEP pins are owned by the RMT
     * peripheral and configured by motors_rmt_init().
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
    /* Bench bring-up: the wiring gives positive rotation with DIR LOW, so the
     * positive/negative mapping is inverted vs. a direct connection. */
    int dir = direction == MOTOR_DIRECTION_POSITIVE ? 0 : 1;
    if (last_dir_ra != dir) {
        last_dir_ra = dir;
        gpio_set_level(RA_DIR_GPIO, dir);
        esp_rom_delay_us(DIR_SETUP_TIME_US);
    }
}

void motors_hw_set_direction_dec(MotorDirection direction) {
    /* Bench bring-up: the wiring gives positive rotation with DIR LOW, so the
     * positive/negative mapping is inverted vs. a direct connection. */
    int dir = direction == MOTOR_DIRECTION_POSITIVE ? 0 : 1;
    if (last_dir_dec != dir) {
        last_dir_dec = dir;
        gpio_set_level(DEC_DIR_GPIO, dir);
        esp_rom_delay_us(DIR_SETUP_TIME_US);
    }
}
