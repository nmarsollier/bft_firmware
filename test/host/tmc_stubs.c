/* tmc_stubs.c — host stub for the TMC2209 runtime microstep switch.
 *
 * The host regression compiles the REAL motors_microsteps.c, which reaches
 * into the TMC layer to change CHOPCONF.MRES, to change GCONF.en_SpreadCycle
 * (chopper mode) and to query the active values.  The single-wire UART driver
 * is hardware-only, so this stub models the state the real driver keeps: the
 * setters record the value, the getters return it.  The tests exercise the
 * selection/accounting logic, not the wire protocol (covered by the proto
 * acceptance suite on real hardware).
 */
#include "tmc.h"
#include "config.h"

static uint16_t s_microsteps = MOTORS_MICROSTEPS_TRACKING;
static TmcChopMode s_chop_mode = TMC_CHOP_STEALTH;

esp_err_t tmc2209_set_microsteps(uint16_t microsteps) {
    s_microsteps = microsteps;
    return ESP_OK;
}

uint16_t tmc2209_get_microsteps(void) {
    return s_microsteps;
}

esp_err_t tmc2209_set_chop_mode(TmcChopMode mode) {
    s_chop_mode = mode;
    return ESP_OK;
}

TmcChopMode tmc2209_get_chop_mode(void) {
    return s_chop_mode;
}
