/* Motors - motors_init.c
 *
 * Purpose: initialize the motors module — state, hardware, queue, and motion task.
 */
#include "motors.h"
#include "motors_internal.h"

#include "esp_log.h"
#include "esp_err.h"

/*
 * Default motors state — positions in microsteps, home = 0.
 * Axis limits are configured via .limits (see MotorsState).
 */
MotorsState motors_state = {
    .ra_steps = 0,
    .dec_steps = 0,
    .status = MOTORS_STATUS_READY,
    .tracking = TRACKING_NONE,
    .ra_speed = 0.0f,
    .dec_speed = 0.0f,
};

/*
 * Spinlock guarding the int64_t position counters (ra_steps / dec_steps)
 * against torn reads from other cores.  On a 32-bit target an int64_t
 * access is two 32-bit accesses, so readers/writers must serialize.
 */
portMUX_TYPE motors_state_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t motors_init(void) {
    esp_err_t err;

    /* Load persisted axis limits (or factory defaults on first boot). */
    motors_limits_load();

    err = motors_hw_init();
    if (err != ESP_OK) {
        ESP_LOGE("MOTORS_INIT", "motors_hw_init: %s", esp_err_to_name(err));
        motors_enter_hardware_fault();
        return err;
    }

    err = motors_queue_init();
    if (err != ESP_OK) {
        ESP_LOGE("MOTORS_INIT", "motors_queue_init failed: %s", esp_err_to_name(err));
        motors_enter_hardware_fault();
        return err;
    }

    err = motors_motion_task_init();
    if (err != ESP_OK) {
        ESP_LOGE("MOTORS_INIT", "motors_motion_task_init failed: %s", esp_err_to_name(err));
        motors_enter_hardware_fault();
        return err;
    }

    return ESP_OK;
}
