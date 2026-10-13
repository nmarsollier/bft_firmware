/* Motors - motors_unpark.c
 *
 * Purpose: leave the parked state and return to READY.
 *
 * PARKED restricts all movement; this is the only transition out of it.
 * STOP deliberately preserves PARKED, so a stray stop cannot unpark.
 */
#include "motors.h"
#include "motors_internal.h"

MotorResultCode motors_unpark(void) {
    if (motors_status_is_error(motors_state.status)) {
        return MOTOR_ERR_HARDWARE_ERROR;
    }

    portENTER_CRITICAL(&motors_state_lock);
    if (motors_state.status == MOTORS_STATUS_PARKED) {
        motors_state.status = MOTORS_STATUS_READY;
    }
    portEXIT_CRITICAL(&motors_state_lock);
    return MOTOR_OK;
}
