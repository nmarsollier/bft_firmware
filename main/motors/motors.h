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

    /* True when the step counters may not match the physical axes — an
     * emergency abort (power loss / hardware fault) cut an RMT batch
     * mid-emission, so some emitted microsteps were never counted.  Cleared
     * when the origin is re-established (set-current-as-home). */
    bool position_uncertain;

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
 * Update the motors' view of the TMC2209 drivers being alive (from the UART
 * probe).  When the drivers go silent the motors enter ERROR and reject all
 * motion; when they answer again the drivers are re-initialised and the motors
 * recover to READY (only if the error was caused by the power loss).
 */
void motors_update_power(bool power_ok);

/* True when both TMC2209 drivers answered the last probe (cached). */
bool motors_has_power(void);

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

/* Convert an axis angle in degrees to the nearest absolute microstep counter,
 * using the active microstep resolution.  Public so the mount layer can
 * validate a destination with the exact conversion the motion task executes. */
int64_t motors_deg_to_steps(float degrees);

/*
 * Current active microstep resolution on both drivers (MOTORS_MICROSTEPS_TRACKING or the
 * coarse MOTORS_MICROSTEPS_SLEW during a large slew).  Public so the protocol
 * layer can report it in the state snapshot.
 */
uint16_t motors_microsteps_active(void);

/*
 * sin(geometric altitude) of a mechanical position at a site latitude (MOUNT.md
 * §9).  Pure — used by both the mount layer and the motion task.
 */
double motors_altitude_sin(float ra_axis_deg, float dec_axis_deg, float lat);

/*
 * True when every point in the axis-aligned rectangle from (ra_from, dec_from)
 * to (ra_to, dec_to) is at or above the geometric horizon.  Conservative: a
 * slew moves each axis monotonically, so the rectangle contains the whole
 * trajectory (MOUNT.md §10).
 */
bool motors_path_above_horizon(float ra_from, float dec_from,
                               float ra_to, float dec_to, float lat);

/*
 * True when no motion is active or queued (status not SLEWING/TRACKING, no
 * active guide pulse, no pending command).  The mount layer uses this to reject
 * a site-latitude change while movement is active or pending (MOUNT.md §10).
 */
bool motors_is_quiescent(void);

/*
 * True when the mount is slewing (high-speed motion).  The runtime power probe
 * skips its UART read while slewing — the UART activity would disturb the RMT
 * step stream — but reads during tracking (slow) and idle.
 */
bool motors_is_slewing(void);

/*
 * Estimated single-axis slew duration (seconds) for a distance and requested
 * speed, using the real accel/decel ramp profile (not a constant velocity).
 * Public so the mount layer can compensate a destination for the sky's drift
 * over the actual travel time.
 */
float motors_slew_duration_estimate(float distance_deg, float speed_dps);


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
 * Return the manual move-axis angular speed (deg/s) for a given speed_rate
 * profile (1..10, higher = faster).  Continuous MoveAxis / joystick motion
 * runs at constant velocity (no acceleration ramp), so this geometric scale
 * is gentle: rate 1 sits near the sidereal rate for fine centering, rate 10
 * is the manual ceiling (1 deg/s).
 */
float motors_get_move_axis_speed(int speed_rate);

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

/* Set the current physical position as the new origin (home).  Returns true on
 * success, false when the motion task could not be confirmed idle (a delayed
 * RMT transmission) — in that case the counters are left untouched. */
bool motors_set_current_as_home(void);

/* Set axis limits from the current physical position.  Each returns true on
 * success, false when the motion task could not be confirmed idle. */
bool motors_set_current_as_ra_left_limit(void);
bool motors_set_current_as_ra_right_limit(void);
bool motors_set_current_as_dec_left_limit(void);
bool motors_set_current_as_dec_right_limit(void);

/* Reset limits to factory defaults and persist.  Returns true on success,
 * false when the motion task could not be confirmed idle. */
bool motors_limits_reset(void);
