/* Motors - motors_power.c
 *
 * Purpose: gate motor motion on the TMC2209 drivers being alive, sensed through
 * the UART probe (the drivers are fed by VM, so they are silent when the 12 V
 * rail is absent).  Both drivers must answer for the mount to move.  When power
 * is lost the motors enter ERROR and stay there until power returns, at which
 * point the TMC2209s are re-initialised (their registers were lost while
 * unpowered).
 */
#include "motors.h"
#include "motors_internal.h"
#include "tmc.h"

#include "esp_log.h"

static const char *TAG = "MOTORS_POWER";

/* Cached power state, from the last TMC probe. */
static bool s_power_ok = true;

/* True when the current ERROR was caused by a power loss (recoverable),
 * as opposed to a hardware fault (unrecoverable). */
static bool s_power_fault = false;

void motors_mark_power_fault(void) {
    s_power_fault = true;
}

bool motors_has_power(void) {
    return s_power_ok;
}

void motors_update_power(bool power_ok) {
    s_power_ok = power_ok;

    if (!power_ok) {
        if (!s_power_fault) {
            ESP_LOGW(TAG, "TMC driver power lost — motors in ERROR");
        }
        motors_enter_error_state();
        s_power_fault = true;
        return;
    }

    if (!s_power_fault) {
        return;  /* power was already on — nothing to do */
    }

    /* Power returned — re-initialise the TMC2209s, whose registers were lost
     * while the drivers were unpowered. */
    s_power_fault = false;

    if (motors_has_hardware_fault()) {
        ESP_LOGI(TAG, "TMC power restored — hardware fault persists");
        return;
    }

    if (tmc2209_reinit() == ESP_OK) {
        ESP_LOGI(TAG, "TMC power restored — motors back to READY");
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.status = MOTORS_STATUS_READY;
        motors_state.tracking = TRACKING_NONE;
        portEXIT_CRITICAL(&motors_state_lock);
    } else {
        ESP_LOGE(TAG, "TMC power restored but re-init failed — hardware fault");
        motors_enter_hardware_fault();
    }
}
