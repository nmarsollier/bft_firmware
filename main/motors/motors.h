#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * High-level state of the motors subsystem used as the authoritative
 * source of truth for mount activity.
 */

typedef enum {
    /* Ready to accept slews/tracking requests. */
    MOTORS_STATUS_READY,
    /* Performing a user-initiated slew to coordinates. */
    MOTORS_STATUS_SLEWING,
    /* Continuous tracking is active. */
    MOTORS_STATUS_TRACKING,
    /* Parked: mount in safe parked position. */
    MOTORS_STATUS_PARKED,
    /* Unrecoverable hardware error at boot. Only reboot clears it. */
    MOTORS_STATUS_ERROR
} MotorsStatus;

/*
 * High-level tracking profiles the motors module can apply.
 */
typedef enum TrackingMode {
    /* No automatic tracking. */
    TRACKING_NONE,
    /* Sidereal tracking (stars). */
    TRACKING_SIDEREAL,
    /* Lunar tracking (moon). */
    TRACKING_LUNAR,
    /* Solar tracking (sun). */
    TRACKING_SOLAR
} TrackingMode;

/*
 * Tracking angular speeds (deg/s) — apparent sky motion at the equator.
 *
 * SIDEREAL  = 360° / 86164.0905308 s  (sidereal day)
 * SOLAR     = 360° / 86400 s          (mean solar day, annual mean)
 * LUNAR     = SIDEREAL − 360° / (27.321661 × 86400)  (sidereal month)
 */
#define TRACKING_SPEED_SIDEREAL_DPS  0.004178074f
#define TRACKING_SPEED_SOLAR_DPS     0.004166667f
#define TRACKING_SPEED_LUNAR_DPS     0.004025576f

/*
 * Authoritative snapshot of the motors module's view of the mount.
 *
 * Position fields are absolute microstep counters (int64_t) for zero
 * accumulation error.  Use motors_get_ra_deg() / motors_get_dec_deg()
 * to read the current position in degrees.
 */
typedef struct {
    /* Current RA axis position — absolute microstep counter. */
    int64_t ra_steps;
    /* Current DEC axis position — absolute microstep counter. */
    int64_t dec_steps;
    /* High-level motors-derived status. */
    MotorsStatus status;
    /* Current requested tracking mode. */
    TrackingMode tracking;
    /* Current commanded/measured RA axis angular speed (deg/s). */
    float ra_speed;
    /* Current commanded/measured DEC axis angular speed (deg/s). */
    float dec_speed;
    /* True when a PulseGuide is active on either axis. */
    bool guiding;

    /* Operational limits enforced by the motors module. */
    struct {
        float ra_min;
        float ra_max;
        float dec_min;
        float dec_max;
    } limits;
} MotorsState;

/* Numeric result codes returned by motors functions. Mount maps these to MountResult objects. */
typedef enum {
    MOTOR_OK = 0,
    MOTOR_ERR_INVALID_AXIS = 1,
    MOTOR_ERR_OUT_OF_RANGE = 2,
    MOTOR_ERR_NOT_READY = 3,
    MOTOR_ERR_HARDWARE_ERROR = 4,
    MOTOR_ERR_PARKED = 5,
    MOTOR_ERR_BUSY = 6
} MotorResultCode;

/*
 * Initialize the motors subsystem.  Returns ESP_OK on success,
 * otherwise the subsystem is left in ERROR state.
 */
esp_err_t motors_init(void);

/*
 * Put the motors subsystem into the unrecoverable ERROR state: aborts any
 * in-flight motion and sets MOTORS_STATUS_ERROR.  Used at boot when a
 * required peripheral fails to initialise.  Only a
 * reboot clears this state.
 */
void motors_enter_error_state(void);

/*
 * Update the mount's view of the external-power rail.  When power is lost
 * the motors enter ERROR and reject all motion; when power returns they
 * recover to READY (only if the error was caused by the power loss).
 */
void motors_update_power(bool power_ok);

/*
 * Return a snapshot copy of the current `MotorsState`.
 */
MotorsState motors_current_state(void);

/*
 * Current axis positions in degrees — derived view over the int64_t
 * microstep counters.  No accumulation error.
 */
float motors_get_ra_deg(void);

float motors_get_dec_deg(void);

/* Convert an absolute microstep counter to degrees.  Public so the protocol
 * layer can derive degrees from a single coherent MotorsState snapshot rather
 * than re-reading each axis separately. */
float motors_steps_to_deg(int64_t steps);

/*
 * Stop both axes and return to READY.
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_stop(void);

/*
 * Park both axes: stop motion and set status to PARKED.
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_park(void);

/*
 * Leave the parked state and return to READY.
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_unpark(void);

/*
 * Move the mount to the home position (0, 0).
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_home(void);

/*
 * Start continuous tracking according to the chosen `TrackingMode`.
 */
MotorResultCode motors_start_tracking(TrackingMode mode);

/*
 * Move one or both axes continuously at the given rates in deg/s.
 * Positive = forward, negative = reverse, zero = stop that axis.
 * Both zero is equivalent to STOP.  Used by MoveAxis and manual controls.
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_set_move_axis_speed(float ra_speed, float dec_speed);

/*
 * Return the slewing angular speed (deg/s) for a given speed_rate profile.
 */
float motors_get_slewing_speed(int speed_rate);

/*
 * Canonical status and tracking name helpers.
 */
const char *motors_status_to_string(MotorsStatus status);

/* True if `status` is a fatal error state (motor fault). */
bool motors_status_is_error(MotorsStatus status);

/* True if `status` is the parked state (movement requires unpark first). */
bool motors_status_is_parked(MotorsStatus status);

const char *motors_tracking_to_string(TrackingMode tracking);

TrackingMode motors_tracking_from_string(const char *value);

const char *motors_tracking_valid_values(void);

/* Move both axes to absolute angles in degrees. */
MotorResultCode motors_slew_to_angle(float ra_deg, float dec_deg, int speed_rate);

MotorResultCode motors_slew_axis_ra(float degrees, int speed_rate);

MotorResultCode motors_slew_axis_dec(float degrees, int speed_rate);

/*
 * Enqueue a PulseGuide command.  The motion task dequeues and executes
 * it — no direct state writes from the caller.  Works whether tracking
 * is active or not.
 *
 * axis: 0 = RA, 1 = DEC
 * offset_dps: signed angular speed offset in deg/s
 * duration_ms: guide pulse duration in milliseconds
 *
 * Returns MOTOR_ERR_HARDWARE_ERROR if motors are in ERROR state.
 */
MotorResultCode motors_pulse_guide_start(int axis, float offset_dps, uint32_t duration_ms);

/*
 * Return true if a PulseGuide is active on either axis.
 * Reads motors_state.guiding — written exclusively by the motion task.
 */
bool motors_is_pulse_guiding(void);

/* ── Axis limits (NVS-persisted, configurable at runtime) ── */

/* Load limits from NVS (or factory defaults on first boot). */
void motors_limits_load(void);

/* Persist current limits to NVS. */
void motors_limits_save(void);

/* Set the current physical position as the new origin (home). */
void motors_set_current_as_home(void);

/* Set axis limits from the current physical position. */
void motors_set_current_as_ra_left_limit(void);
void motors_set_current_as_ra_right_limit(void);
void motors_set_current_as_dec_left_limit(void);
void motors_set_current_as_dec_right_limit(void);

/* Reset limits to factory defaults and persist. */
void motors_limits_reset(void);
