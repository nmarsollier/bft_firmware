#pragma once

#include "mount.h"

#include <stdbool.h>
#include <stdint.h>

#include "motors/motors.h"

/* Internal mount state stored by the mount module. */
extern MountSettings mount_internal_state;

/* True once a valid system time has been set — gates GOTO. */
extern bool mount_time_valid;

MountResult mount_result_ok(void);

MountResult mount_result_error(const char *message);

/* NVS helpers used to persist mount settings. */
void mount_settings_load(MountSettings *out_settings);

void mount_settings_save(const MountSettings *settings);

MountResult motors_result_code_error_result(MotorResultCode rc);

/*
 * Check whether the mount is in the unrecoverable ERROR state (motor fault).
 * All mount command functions gate on this
 * before delegating to motors.
 */
static inline bool mount_is_error(void) {
    return motors_status_is_error(motors_current_state().status);
}

static inline MountResult mount_result_error_state(void) {
    return mount_result_error("Mount in error state — reboot required");
}
