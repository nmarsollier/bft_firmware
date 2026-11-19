#pragma once

#include "motors.h"
#include "config.h"

#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* =========================================================================
 * Motion command queue — thread-safe communication with the motion task.
 *
 * External callers (protocol handlers, button poller) send MotionCommand
 * structs to the queue.  The motors task is the sole consumer and the
 * sole writer of motors_state position fields.
 *
 * Only motion-producing commands go through the queue.  Stop / park
 * are handled directly by their callers via
 * motors_motion_stop() + motors_state update — no queue round-trip.
 * ========================================================================= */

typedef enum {
    MOTION_CMD_SLEW = 0,
    MOTION_CMD_TRACK,
    MOTION_CMD_MOVE_AXIS,
    MOTION_CMD_PULSE_GUIDE,
} MotionCommandType;

typedef struct {
    MotionCommandType type;
    float ra_target_deg;
    float dec_target_deg;
    float ra_speed;
    float dec_speed;
    TrackingMode tracking_mode;
    /* Tracking mode to resume once a SLEW completes (motors_slew_to_angle /
     * motors_slew_axis).  Carried on the SLEW itself so the slew + tracking
     * resume are one atomic operation — never enqueued separately. */
    TrackingMode resume_tracking;
    bool relative;
    float ra_delta_deg;
    float dec_delta_deg;
    /* PulseGuide fields (valid when type == MOTION_CMD_PULSE_GUIDE) */
    int guide_axis;        /* 0 = RA, 1 = DEC */
    float guide_offset_dps; /* signed deg/s */
    uint32_t guide_duration_ms;
    /* Stop epoch captured at enqueue — a command whose epoch is stale
     * (a STOP landed after it was enqueued) is discarded at execution. */
    uint32_t generation;
} MotionCommand;

/* Queue handle — created by motors_init(), shared across the module. */
extern QueueHandle_t motion_cmd_queue;

/* Stop epoch — incremented on every stop so queued commands can be
 * invalidated.  Read/written from multiple tasks (uint32, monotonic). */
extern volatile uint32_t motors_stop_generation;

/* Spinlock guarding the int64_t position counters against torn cross-core
 * reads (motors_current_state / motors_get_*_deg). */
extern portMUX_TYPE motors_state_lock;

/* Send a MotionCommand to the back of the queue (FIFO).
 * Returns false if the queue is full or uninitialised. */
bool motors_queue_put(MotionCommand *cmd);

/* Atomically discard every command in the queue. */
void motors_queue_clear(void);

/* Validate axis values against the configured inclusive limits. */
bool motors_is_valid_ra(float value);

bool motors_is_valid_dec(float value);

bool motors_is_valid_ra_steps(int64_t steps);

bool motors_is_valid_dec_steps(int64_t steps);

/* =========================================================================
 * Mechanical constants — hardware configuration.
 * ========================================================================= */

/* Maximum safe slew speed in deg/s — hardware ceiling for this reduction. */
#define MOTORS_MAX_SLEW_SPEED_DPS 4.0f

/* Maximum manual move-axis speed in deg/s — ceiling for continuous
 * MoveAxis / joystick motion.  Lower than the slew ceiling because manual
 * motion runs at constant velocity (no acceleration ramp). */
#define MOTORS_MAX_MOVE_AXIS_SPEED_DPS 1.0f

/*
 * Angular displacement per microstep at the mount axis.
 *
 *   1.8° = NEMA 17 full-step angle
 *   MOTORS_MICROSTEPS_TRACKING        = 64 (TMC2209 CHOPCONF.MRES)
 *   TOTAL_GEAR_REDUCTION     = 500:1 (5:1 belts × 100:1 harmonic drive)
 */
static inline float motors_get_deg_per_microstep(void) {
    return 1.8f / ((float) MOTORS_MICROSTEPS_TRACKING *
                    TOTAL_GEAR_REDUCTION);
}

/* =========================================================================
 * Runtime microstep resolution (motors_microsteps.c)
 *
 * The STEP counters stay in canonical MOTORS_MICROSTEPS_TRACKING units regardless of
 * the active driver resolution.  A large ramped slew switches both drivers to
 * MOTORS_MICROSTEPS_SLEW and emits one STEP pulse per `motors_steps_per_pulse()`
 * canonical steps; every other motion runs at MOTORS_MICROSTEPS_TRACKING (factor 1).
 * ========================================================================= */

/* Canonical counter steps advanced by a single STEP pulse at the active resolution. */
uint32_t motors_steps_per_pulse(void);

/* Select the resolution for a motion command: a ramped SLEW uses the coarse
 * resolution when either axis travels more than MOTORS_SLEW_GENTLE_DEG; every
 * other command type uses fine.  `ra_dist`/`dec_dist` are the canonical axis
 * distances (meaningful only for SLEW).  Returns false (and puts the mount into
 * ERROR) when the driver switch could not be verified. */
bool motors_microsteps_prepare(MotionCommandType type, int64_t ra_dist, int64_t dec_dist);

/* =========================================================================
 * Slew ramp profile (motors_ramp.c)
 * ========================================================================= */

/* Effective velocity (deg/s) for one axis during a slew: distance-based
 * trapezoidal profile from MIN_SLEW_CDS up to the (capped) target and back.
 * Pure — same inputs always produce the same output. */
float ramp_velocity(int target_vel_cds, int64_t travelled_steps,
                    int64_t distance_steps, int distance_cds);

/* =========================================================================
 * Hardware layer — DIR GPIO control (motors_hw.c).
 *
 * STEP pulse generation is handled by the RMT peripheral (motors_rmt.c).
 * The TMC2209 drivers are always enabled (no EN pin connected), so there is
 * no enable GPIO to manage.  ALARM pins are not connected.
 * ========================================================================= */

typedef enum {
    MOTOR_DIRECTION_NEGATIVE = 0,
    MOTOR_DIRECTION_POSITIVE = 1,
} MotorDirection;

esp_err_t motors_hw_init(void);

void motors_hw_set_direction_ra(MotorDirection direction);

void motors_hw_set_direction_dec(MotorDirection direction);

/* =========================================================================
 * RMT+DMA step pulse generation (motors_rmt.c).
 *
 * Replaces software GPIO bit-banging with hardware-timed RMT symbols
 * streamed via GDMA. Zero jitter, near-zero CPU.
 * ========================================================================= */

/* RMT clock resolution — 2 MHz (0.5 us per tick).
 * Balanced for high-reduction configurations while keeping slow-step
 * idle symbols within SOC_RMT_MEM_WORDS_PER_CHANNEL (48). */
#define RMT_RESOLUTION_HZ 2000000U

/* STEP pulse timing in RMT ticks (2 MHz reference).
 *
 * Driver (TMC2209): one microstep per accepted STEP pulse (rising edge).
 * The STEP input is 3.3 V CMOS, driven directly (no level shifting), so a
 * 2 µs HIGH pulse is ample margin. */
#define STEP_PULSE_TICKS     4U    /* 2 us HIGH — TMC2209 direct drive */
#define STEP_MIN_LOW_TICKS   4U    /* 2 us LOW floor */
#define STEP_MIN_PERIOD_TICKS (STEP_PULSE_TICKS + STEP_MIN_LOW_TICKS)  /* 8 ticks = 4 us */

/* =========================================================================
 * Position representation — int64_t absolute microstep counters.
 *
 * Degrees are a derived view over the step counter, computed on demand
 * for API consumers.  All internal position tracking uses integer steps
 * for zero-accumulation-error precision over arbitrarily long sessions.
 * ========================================================================= */

/* Convert between steps and degrees using the active microstep resolution.
 * Defined in motors_current_state.c; declared public in motors.h. */
float motors_steps_to_deg(int64_t steps);

esp_err_t motors_rmt_init(void);

uint32_t motors_rmt_encode_steps(rmt_symbol_word_t *symbols,
                                  uint32_t max_symbols,
                                  uint32_t step_period_ticks,
                                  uint32_t step_count);

/*
 * Encode a bare STEP pulse (HIGH + minimal LOW).
 * For use by the tracking/guiding loop where inter-step timing
 * is handled by the accumulator + deadline, not by RMT symbols.
 * Always consumes 1 symbol.
 */
uint32_t motors_rmt_encode_pulse(rmt_symbol_word_t *symbols);

esp_err_t motors_rmt_transmit_ra(const rmt_symbol_word_t *symbols,
                                  uint32_t num_symbols);

esp_err_t motors_rmt_transmit_dec(const rmt_symbol_word_t *symbols,
                                   uint32_t num_symbols);

/*
 * Pipelined transmit — no semaphore drain before launching.
 * Used by the double-buffered slewing loop after the first batch.
 */
esp_err_t motors_rmt_transmit_no_drain_ra(const rmt_symbol_word_t *symbols,
                                           uint32_t num_symbols);

esp_err_t motors_rmt_transmit_no_drain_dec(const rmt_symbol_word_t *symbols,
                                            uint32_t num_symbols);

esp_err_t motors_rmt_wait_ra(TickType_t timeout_ticks);

esp_err_t motors_rmt_wait_dec(TickType_t timeout_ticks);

bool motors_rmt_try_wait_ra(void);

bool motors_rmt_try_wait_dec(void);

/* Recreate both RMT channels — motion task only. */
void motors_rmt_reset_both(void);

/* =========================================================================
 * Module-global state — motors_state is the single source of truth for
 * the motors layer.  External code reads it through motors_current_state().
 * ========================================================================= */
extern MotorsState motors_state;

/* In-flight motion runtime state — set up by process_command() and consumed by
 * the two motion loops (motors_motion_task.c).  `active`/`aborted`/`graceful`
 * and the guide deadline/offset fields are also written from any task by
 * motors_motion_stop() / motors_motion_stop_graceful() (their own .c files) to
 * signal an abort; the remaining fields are single-writer (motion task only). */
typedef struct {
    volatile bool active;
    volatile bool aborted;
    volatile bool graceful;
    MotionCommandType active_cmd_type;
    int64_t ra_target;         /* steps */
    int64_t dec_target;        /* steps */
    int64_t ra_start;          /* steps — captured at motion start (for ramps) */
    int64_t dec_start;         /* steps */
    TrackingMode resume_tracking;
    int64_t ra_guide_deadline_us;
    int64_t dec_guide_deadline_us;
    float ra_guide_offset_dps;
    float dec_guide_offset_dps;
} MotionState;

extern MotionState s_motion;

/* Latched hardware-fault tracking — see motors_enter_error_state.c.
 * A hardware fault is never cleared by a power-loss recovery. */
void motors_enter_hardware_fault(void);
bool motors_has_hardware_fault(void);

/* Mark the current ERROR as a recoverable power fault so a later power-on
 * re-initialises the drivers (motors_power.c). */
void motors_mark_power_fault(void);

float motors_get_tracking_speed(TrackingMode mode);

/* Motion task handle — exposed so external code can send notifications. */
extern TaskHandle_t motors_motion_task_handle;

/* =========================================================================
 * Task & queue lifecycle (motors_task.c, motors_queue.c).
 * ========================================================================= */

/* Stop the active motion loop from outside the motion task.
 * Owns s_motion.active and task notification.
 * Safe to call from any task. */
void motors_motion_stop(void);

/* Graceful stop — request the motion loop to stop submitting new batches but
 * first confirm + account the batches already committed to RMT, keeping the
 * position counters exact.  Used by motors_stop() / motors_park(). */
void motors_motion_stop_graceful(void);

/* Dequeue and execute one motion command (extracted from the task loop).
 * Exposed so the host regression can drive the real scheduler one command at a
 * time with simulated clock/RTOS/peripherals. */
void motors_motion_process_one(void);

/* ── Motion idle signalling (motors_motion_idle.c) ──
 * The idle semaphore is private to that file; the motion task and the control
 * operations both go through these functions. */

/* Create the idle semaphore.  Called once by the motion task init. */
esp_err_t motors_motion_idle_init(void);

/* The motion task signals it is idle (about to block on the command queue). */
void motors_motion_idle_give(void);

/* The motion task signals it is busy (it just dequeued a command). */
void motors_motion_idle_take(void);

/* Wait (bounded) until the motion task is idle.  Returns true only when idle
 * was confirmed (the semaphore was taken); false on timeout means the motion
 * task is still running and the caller must not modify counters/limits. */
bool motors_motion_wait_idle(void);

esp_err_t motors_motion_task_init(void);

esp_err_t motors_queue_init(void);
