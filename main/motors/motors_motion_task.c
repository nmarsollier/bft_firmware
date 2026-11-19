/* Motors - motors_motion_task.c
 *
 * Purpose: FreeRTOS motion task — consumes MotionCommands from the queue
 * and drives axis positions via RMT+DMA step pulse generation.
 *
 * The motion task is the sole writer of motors_state position fields
 * (ra_steps, dec_steps) during slews and tracking.
 * Status and tracking fields are updated cooperatively by the motion
 * task, motors_stop(), and motors_park().
 *
 * Two execution paths, dispatched by command type:
 *   slewing_loop_rmt  — distance-bounded, ramped accel/decel, batched RMT
 *   tracking_loop_rmt — open-ended, constant velocity, fractional accumulator
 *
 * RMT replaces the previous software GPIO bit-banging. Step pulse shapes
 * are hardware-timed; the pulse start still depends on the scheduler.
 * The CPU sleeps on a semaphore while the RMT peripheral streams symbols.
 */

#include "motors_internal.h"

#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "MOTORS_MOTION_TASK";

/*
 * Task stack size — sized to accommodate batch encoding loops,
 * condition checks, and FreeRTOS queue operations.
 */
#define MOTION_TASK_STACK_BYTES 32768  /* 32 KB — room for 2×512-symbol ping-pong buffers + RMT driver.  xTaskCreatePinnedToCore takes BYTES. */
#define MOTION_TASK_PRIORITY    23  /* near real-time */

/* Step period ceiling in RMT ticks — used when velocity is zero or unknown. */
#define MAX_STEP_PERIOD_TICKS (20U * 1000U * 1000U) /* 10 s at 2 MHz */

/* --------------------------------------------------------------------------
 * RMT batch control
 * -------------------------------------------------------------------------- */

/*
 * Target batch duration in RMT ticks.  Shorter batches give finer ramp
 * granularity but increase CPU overhead.  40k ticks = 20 ms at 2 MHz.
 */
#define RMT_BATCH_TARGET_TICKS  320000U

/*
 * Buffer and batch limits — unified for both axes.
 * 512 symbols (2 KB) per ping-pong buffer, batch ≤ 512 steps.
 *
 * The batch cap sets the duration of the *pre-generated window*.  At max slew
 * (4 °/s) a 512-step batch streams ~7.2 ms, so the motion task has a full batch
 * duration to encode + queue the next window before the current one drains.
 * The previous 100-step cap made the high-speed window only ~1.4 ms — short
 * enough that any scheduling jitter left a gap in the step stream.
 *
 * RMT_BATCH_MAX_STEPS also bounds the position error of an *emergency* abort:
 * a fault (power loss / hardware error) that cuts a batch mid-emission leaves
 * at most this many microsteps uncounted, so the position is marked uncertain
 * instead of silently trusted.  A graceful stop (motors_stop / motors_park)
 * drains the committed batch and keeps the position exact.
 */
#define RMT_BUFFER_SYMBOLS  512U
#define RMT_BATCH_MAX_STEPS  512U

TaskHandle_t motors_motion_task_handle = NULL;

/* Stop epoch — bumped on every stop so commands enqueued before the stop
 * can be discarded by the motion task. */
volatile uint32_t motors_stop_generation = 0;

/* Motion runtime state — the shared MotionState from motors_internal.h.
 * The motion task is the single writer of the command fields (targets, starts,
 * resume_tracking); motors_motion_stop*() write the abort/guide-clear fields
 * from any task. */
MotionState s_motion;

/* --------------------------------------------------------------------------
 * Step period helpers
 * -------------------------------------------------------------------------- */

/*
 * Convert an angular speed (deg/s) to a step period in RMT ticks.
 * Uses the runtime microstep resolution from the motor configuration.
 * Returns MAX_STEP_PERIOD_TICKS when velocity is effectively zero.
 */
static uint32_t step_period_ticks(float velocity_dps) {
    if (fabsf(velocity_dps) < 1e-9f)
        return MAX_STEP_PERIOD_TICKS;

    /* Period of one STEP pulse: canonical degrees per step × the pulse
     * multiplier of the active microstep resolution. */
    float deg_per_pulse = motors_get_deg_per_microstep()
                        * (float) motors_steps_per_pulse();
    float period_s = deg_per_pulse / fabsf(velocity_dps);
    uint32_t ticks = (uint32_t) (period_s * (float) RMT_RESOLUTION_HZ);
    if (ticks < STEP_MIN_PERIOD_TICKS)
        ticks = STEP_MIN_PERIOD_TICKS;
    return ticks;
}

/* --------------------------------------------------------------------------
 * Motion helpers — small reusable blocks extracted from the hot paths.
 * -------------------------------------------------------------------------- */

/* Clamp a batch to remaining steps and the hard max-step ceiling. */
static uint32_t compute_batch_size(uint32_t period_ticks, uint32_t remaining) {
    uint32_t batch = (period_ticks < RMT_BATCH_TARGET_TICKS)
                         ? RMT_BATCH_TARGET_TICKS / period_ticks
                         : 1;

    if (batch > remaining)
        batch = remaining;

    if (batch > RMT_BATCH_MAX_STEPS)
        batch = RMT_BATCH_MAX_STEPS;

    return batch;
}

/* Clamp an absolute step target to the configured RA/DEC limits. */
static int64_t clamp_ra_steps(int64_t steps) {
    int64_t lo = motors_deg_to_steps(motors_state.limits.ra_min);
    int64_t hi = motors_deg_to_steps(motors_state.limits.ra_max);
    if (steps < lo) return lo;
    if (steps > hi) return hi;
    return steps;
}

static int64_t clamp_dec_steps(int64_t steps) {
    int64_t lo = motors_deg_to_steps(motors_state.limits.dec_min);
    int64_t hi = motors_deg_to_steps(motors_state.limits.dec_max);
    if (steps < lo) return lo;
    if (steps > hi) return hi;
    return steps;
}

/*
 * Clamp a batch so the axis never emits a step past its configured limit.
 * `current` is the absolute position before this batch; `sign` is the travel
 * direction (±1).  Applied per batch so a limit changed while a move is in
 * flight still stops the motion at the new boundary.
 */
static uint32_t clamp_ra_batch(int64_t current, int sign, uint32_t batch) {
    int64_t limit = (sign > 0) ? motors_deg_to_steps(motors_state.limits.ra_max)
                               : motors_deg_to_steps(motors_state.limits.ra_min);
    int64_t headroom = (sign > 0) ? (limit - current) : (current - limit);
    /* `current`/limit are canonical steps; `batch` is STEP pulses. */
    uint32_t max_steps = (headroom > 0)
                             ? (uint32_t)(headroom / (int64_t) motors_steps_per_pulse())
                             : 0;
    return (batch > max_steps) ? max_steps : batch;
}

static uint32_t clamp_dec_batch(int64_t current, int sign, uint32_t batch) {
    int64_t limit = (sign > 0) ? motors_deg_to_steps(motors_state.limits.dec_max)
                               : motors_deg_to_steps(motors_state.limits.dec_min);
    int64_t headroom = (sign > 0) ? (limit - current) : (current - limit);
    uint32_t max_steps = (headroom > 0)
                             ? (uint32_t)(headroom / (int64_t) motors_steps_per_pulse())
                             : 0;
    return (batch > max_steps) ? max_steps : batch;
}

/* Clean shutdown: mark motion finished and return to READY. */
static void finish_motion(void) {
    s_motion.active = false;
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.status = MOTORS_STATUS_READY;
    motors_state.tracking = TRACKING_NONE;
    portEXIT_CRITICAL(&motors_state_lock);
}

/* Emergency abort: an RMT transmit/wait failed.  This is a hardware fault —
 * publish it so the client can tell it apart from a normal completion, and
 * clear BOTH guide deadlines/offsets so a stale deadline cannot re-assert
 * `guiding` after the task returns. */
static void abort_motion(void) {
    s_motion.aborted = true;
    s_motion.active = false;
    s_motion.ra_guide_deadline_us = 0;
    s_motion.ra_guide_offset_dps = 0.0f;
    s_motion.dec_guide_deadline_us = 0;
    s_motion.dec_guide_offset_dps = 0.0f;
    motors_enter_hardware_fault();
}

/* Recompute RA / DEC step-period ticks from current ramp or constant speed. */
static void compute_slew_periods(
        bool is_move_axis,
        int64_t ra_travelled,
        int64_t dec_travelled,
        int64_t ra_dist,
        int64_t dec_dist,
        int ra_distance_cds,
        int dec_distance_cds,
        uint32_t *ra_period,
        uint32_t *dec_period)
{
    if (is_move_axis) {
        *ra_period = step_period_ticks(motors_state.ra_speed);
        *dec_period = step_period_ticks(motors_state.dec_speed);
        return;
    }

    float ra_vel = ramp_velocity(
        (int)(motors_state.ra_speed * 100.0f),
        ra_travelled, ra_dist, ra_distance_cds);
    float dec_vel = ramp_velocity(
        (int)(motors_state.dec_speed * 100.0f),
        dec_travelled, dec_dist, dec_distance_cds);

    *ra_period = step_period_ticks(ra_vel);
    *dec_period = step_period_ticks(dec_vel);
}

/*
 * Extend or replace a PulseGuide deadline for one axis.
 * Same direction extends, opposite direction replaces.
 */
static void update_guide(
        int64_t *deadline_us,
        float *offset_dps,
        const MotionCommand *cmd)
{
    int64_t new_deadline = esp_timer_get_time()
                         + (int64_t)cmd->guide_duration_ms * 1000;

    if (*deadline_us && *offset_dps == cmd->guide_offset_dps) {
        if (new_deadline > *deadline_us)
            *deadline_us = new_deadline;
        return;
    }

    *deadline_us = new_deadline;
    *offset_dps = cmd->guide_offset_dps;
}

/*
 * Emit a single RA tracking step in the given direction.
 * Returns true on success, false if motion must abort (limit hit, RMT error).
 */
static bool emit_ra_tracking_step(int direction, rmt_symbol_word_t *buffer) {
    int64_t next = motors_state.ra_steps + (int64_t)direction;

    if (!motors_is_valid_ra_steps(next)) {
        ESP_LOGW(TAG, "RA limit at %.3f deg",
                 (double)motors_steps_to_deg(motors_state.ra_steps));
        s_motion.active = false;
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.status = MOTORS_STATUS_READY;
        motors_state.tracking = TRACKING_NONE;
        motors_state.guiding = false;
        portEXIT_CRITICAL(&motors_state_lock);
        return false;
    }

    if (!s_motion.active)
        return false;

    motors_rmt_encode_pulse(buffer);
    esp_err_t tx_err = motors_rmt_transmit_ra(buffer, 1);
    if (tx_err != ESP_OK) {
        ESP_LOGE(TAG, "RMT RA tx fail: %s", esp_err_to_name(tx_err));
        abort_motion();
        return false;
    }

    esp_err_t wait_err = motors_rmt_wait_ra(pdMS_TO_TICKS(2000));
    if (wait_err != ESP_OK) {
        ESP_LOGW(TAG, "RMT RA wait %s",
                 (wait_err == ESP_ERR_TIMEOUT) ? "timeout" : "error");
        abort_motion();
        return false;
    }

    if (!s_motion.active)
        return false;

    portENTER_CRITICAL(&motors_state_lock);
    motors_state.ra_steps = next;
    portEXIT_CRITICAL(&motors_state_lock);
    return true;
}

/* --------------------------------------------------------------------------
 * Motion conditions check — returns false when the current motion should end.
 * -------------------------------------------------------------------------- */
static bool check_motion_conditions(void) {
    /*
     * Motor ERROR — immediate abort of any active motion.
     *
     * If the motors subsystem enters ERROR state while slewing or
     * tracking, kill RMT output and physically disable the drivers
     * on the spot.  This is the emergency cut-off: no deceleration,
     * no position cleanup — just stop NOW.
     */
    if (motors_status_is_error(motors_state.status)) {
        motors_enter_error_state();
        s_motion.active = false;
        s_motion.aborted = true;
        return false;
    }

    int64_t ra_diff = s_motion.ra_target - motors_state.ra_steps;
    if (ra_diff < 0) ra_diff = -ra_diff;
    int64_t dec_diff = s_motion.dec_target - motors_state.dec_steps;
    if (dec_diff < 0) dec_diff = -dec_diff;

    bool ra_has_target = (ra_diff > 0);
    bool dec_has_target = (dec_diff > 0);

    /*
     * Slew / move-axis completion: both axes at target.
     *
     * Use active_cmd_type rather than motors_state.status because the
     * caller may have already set status to TRACKING (resume-after-slew
     * pattern in motors_slew_to_angle / motors_slew_axis_*) before the
     * motion task reaches the target.  Checking the authoritative
     * s_motion field guarantees completion is always detected.
     */
    if ((s_motion.active_cmd_type == MOTION_CMD_SLEW ||
         s_motion.active_cmd_type == MOTION_CMD_MOVE_AXIS) &&
        !ra_has_target && !dec_has_target) {
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.status = MOTORS_STATUS_READY;
        motors_state.tracking = TRACKING_NONE;
        portEXIT_CRITICAL(&motors_state_lock);
        s_motion.active = false;

        return false;
    }

    /* External tracking stop. */
    if (motors_state.tracking == TRACKING_NONE &&
        motors_state.status == MOTORS_STATUS_TRACKING) {
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.status = MOTORS_STATUS_READY;
        portEXIT_CRITICAL(&motors_state_lock);
        s_motion.active = false;

        return false;
    }

    return true;
}

/*
 * True when a newer MoveAxis setpoint is queued behind the running one.
 * MoveAxis speeds are live setpoints, not one-shot traversals: a new one
 * must preempt the in-flight continuous move instead of waiting for it to
 * reach the mechanical limit.
 */
static bool move_axis_update_pending(void) {
    MotionCommand queued;
    return xQueuePeek(motion_cmd_queue, &queued, 0) == pdTRUE
           && queued.type == MOTION_CMD_MOVE_AXIS;
}

/* --------------------------------------------------------------------------
 * Command processing — handle one MotionCommand and set up motion state.
 *
 * Only motion-producing commands (SLEW, TRACK, MOVE_AXIS) go through
 * the queue.  Stop / park are handled directly by their
 * callers via motors_motion_stop() + motors_state update.
 * -------------------------------------------------------------------------- */
static void process_command(MotionCommand cmd) {
    /*
     * If motors entered ERROR while this command was queued, reject it
     * without touching state.  The mount layer already guards against
     * enqueuing commands in ERROR, but a race between enqueue and
     * error-detection could still deliver a stale command here.
     */
    if (motors_status_is_error(motors_state.status)) {
        return;
    }

    /* Discard a command invalidated by a STOP that landed after it was
     * enqueued but before it was dequeued here. */
    if (cmd.generation != motors_stop_generation) {
        return;
    }

    s_motion.active_cmd_type = cmd.type;
    s_motion.resume_tracking = TRACKING_NONE;

    switch (cmd.type) {
        case MOTION_CMD_SLEW:
            portENTER_CRITICAL(&motors_state_lock);
            motors_state.ra_speed = cmd.ra_speed;
            motors_state.dec_speed = cmd.dec_speed;
            motors_state.status = MOTORS_STATUS_SLEWING;
            motors_state.tracking = TRACKING_NONE;
            portEXIT_CRITICAL(&motors_state_lock);

            if (cmd.relative) {
                s_motion.ra_target = motors_state.ra_steps
                                     + motors_deg_to_steps(cmd.ra_delta_deg);
                s_motion.dec_target = motors_state.dec_steps
                                      + motors_deg_to_steps(cmd.dec_delta_deg);
            } else {
                s_motion.ra_target = motors_deg_to_steps(cmd.ra_target_deg);
                s_motion.dec_target = motors_deg_to_steps(cmd.dec_target_deg);
            }
            /* Re-clamp at execution: the relative destination is computed from
             * the live position, which may have advanced since the request was
             * validated (queued moves).  A mechanical limit must hold even when
             * two valid moves stack past it. */
            s_motion.ra_target = clamp_ra_steps(s_motion.ra_target);
            s_motion.dec_target = clamp_dec_steps(s_motion.dec_target);
            s_motion.ra_start = motors_state.ra_steps;
            s_motion.dec_start = motors_state.dec_steps;

            s_motion.active = true;
            s_motion.resume_tracking = cmd.resume_tracking;

            break;

        case MOTION_CMD_TRACK:
            portENTER_CRITICAL(&motors_state_lock);
            motors_state.ra_speed = cmd.ra_speed;
            motors_state.dec_speed = 0.0f;
            motors_state.status = MOTORS_STATUS_TRACKING;
            motors_state.tracking = cmd.tracking_mode;
            portEXIT_CRITICAL(&motors_state_lock);

            /*
             * Tracking runs open-ended: target is set to the axis limit so
             * the loop never completes on its own — it only stops when an
             * external status change (STOP, PARK) is detected.
             *
             * RA always tracks positive (hour angle increases at the
             * sidereal rate on both hemispheres), so the target is ra_max;
             * ra_min is the fallback for a negative rate.
             */
            s_motion.ra_target = (cmd.ra_speed >= 0.0f)
                                     ? motors_deg_to_steps(motors_state.limits.ra_max)
                                     : motors_deg_to_steps(motors_state.limits.ra_min);
            s_motion.dec_target = motors_state.dec_steps;
            s_motion.ra_start = motors_state.ra_steps;
            s_motion.dec_start = motors_state.dec_steps;
            s_motion.active = true;

            break;

        case MOTION_CMD_MOVE_AXIS:
            portENTER_CRITICAL(&motors_state_lock);
            motors_state.ra_speed = fabsf(cmd.ra_speed);
            motors_state.dec_speed = fabsf(cmd.dec_speed);
            motors_state.status = MOTORS_STATUS_SLEWING;
            motors_state.tracking = TRACKING_NONE;
            portEXIT_CRITICAL(&motors_state_lock);

            s_motion.ra_target = (cmd.ra_speed > 0.0f)
                                     ? motors_deg_to_steps(motors_state.limits.ra_max)
                                     : (cmd.ra_speed < 0.0f)
                                           ? motors_deg_to_steps(motors_state.limits.ra_min)
                                           : motors_state.ra_steps;
            s_motion.dec_target = (cmd.dec_speed > 0.0f)
                                      ? motors_deg_to_steps(motors_state.limits.dec_max)
                                      : (cmd.dec_speed < 0.0f)
                                            ? motors_deg_to_steps(motors_state.limits.dec_min)
                                            : motors_state.dec_steps;

            s_motion.ra_start = motors_state.ra_steps;
            s_motion.dec_start = motors_state.dec_steps;
            s_motion.active = true;

            break;

        case MOTION_CMD_PULSE_GUIDE:
            /*
             * Revalidate the DEC pole-crossing at the real start position, which
             * may have moved since mount_pulse_guide validated it (a pending
             * pulse, or tracking, advanced the axis in between).  The direction
             * sign flips at a pole (D = 180°·n), so a pulse that would reach or
             * cross it must be dropped, exactly as the mount layer would.
             */
            if (cmd.guide_axis == 1) {
                float dec_axis = motors_steps_to_deg(motors_state.dec_steps);
                float arc_deg = cmd.guide_offset_dps * (cmd.guide_duration_ms / 1000.0f);
                float d_end = dec_axis + arc_deg;
                float pole_eps = motors_steps_to_deg(1);
                float a = fminf(dec_axis, d_end) - pole_eps;
                float b = fmaxf(dec_axis, d_end) + pole_eps;
                int n_lo = (int) ceilf(a / 180.0f);
                int n_hi = (int) floorf(b / 180.0f);
                if (n_lo <= n_hi) {
                    ESP_LOGW(TAG, "Rejecting DEC guide: would cross the pole");
                    break;
                }
            }

            if (cmd.guide_axis == 0) {
                update_guide(&s_motion.ra_guide_deadline_us,
                             &s_motion.ra_guide_offset_dps, &cmd);
            } else {
                update_guide(&s_motion.dec_guide_deadline_us,
                             &s_motion.dec_guide_offset_dps, &cmd);
            }

            portENTER_CRITICAL(&motors_state_lock);
            motors_state.guiding = true;
            portEXIT_CRITICAL(&motors_state_lock);
            /*
             * Set active so a standalone guide pulse (READY, no tracking)
             * actually runs its motion loop.  During tracking active is
             * already true, so this is a no-op there.
             */
            s_motion.active = true;
            break;
    }
}

/* --------------------------------------------------------------------------
 * Slewing & move-axis motion loop — RMT batch scheduling.
 *
 * Replaces software busy-wait with DMA-driven step generation:
 *
 *   1. Determine direction (constant for the entire slew) and set DIR pins.
 *   2. Every ~5 ms recalculate velocity via the ramp curve.
 *   3. Every ~20 ms compute a batch of steps at the current velocity,
 *      encode them as RMT symbols, and transmit via DMA.
 *   4. Block on a semaphore while the RMT peripheral drives the step
 *      timing — near-zero CPU; the pulse shape is exact.
 *   5. On wake-up, update positions by the full batch count and loop.
 *
 * MOVE_AXIS (joystick / NINA centering) skips the ramp — constant
 * velocity from the first microstep so the client's time × rate
 * distance calculations are accurate.
 * -------------------------------------------------------------------------- */
/*
 * Encode one axis batch — shared between the initial batch and pre-encoded
 * batches inside the double-buffered slewing loop.  Preserves the exact same
 * encoding logic (MOVE_AXIS constant-period vs SLEW per-step ramp).
 */
static uint32_t encode_axis_batch(rmt_symbol_word_t *buf, uint32_t max_sym,
                                   uint32_t batch, uint32_t period_ticks,
                                   float speed_dps,
                                   int64_t base_travelled, int64_t dist_steps,
                                   int distance_cds, bool is_move_axis)
{
    if (batch == 0) return 0;

    if (is_move_axis) {
        return motors_rmt_encode_steps(buf, max_sym, period_ticks, batch);
    }

    uint32_t total_sym = 0;
    rmt_symbol_word_t *sym = buf;
    for (uint32_t i = 0; i < batch && total_sym < max_sym; i++) {
        int64_t travelled = base_travelled + (int64_t)i;
        float vel = ramp_velocity((int)(speed_dps * 100.0f),
                                   travelled, dist_steps, distance_cds);
        uint32_t period = step_period_ticks(vel);
        uint32_t n = motors_rmt_encode_steps(sym, max_sym - total_sym,
                                              period, 1);
        sym += n;
        total_sym += n;
    }
    return total_sym;
}

/*
 * Double-buffered slewing / move-axis motion loop.
 *
 * Per-axis ping-pong strategy:
 *   buf[ping]  — currently transmitting via RMT+DMA
 *   buf[pong]  — queued in the TX FIFO behind ping
 *
 *   First:  encode → transmit (with semaphore drain) for both axes
 *   Loop:   advance each axis independently as its ping completes —
 *           confirm → encode → transmit (no drain) → swap that axis's
 *           ping/pong, without waiting for the other axis
 *
 * This eliminates both the software encoding gap (the next batch is
 * already queued and starts with zero gap) and the coupling between
 * axes: a slow axis no longer stalls a fast one.
 */
/*
 * Close the coarse-slew residual: coarse STEP pulses only land on multiples of
 * `spp` canonical steps, so up to spp-1 canonical steps may remain short of the
 * requested target.  Switch to the fine resolution and emit those remaining
 * steps so the final position matches the target exactly.  Returns true on
 * success (or when there is nothing to do); false if the microstep switch
 * failed (mount in ERROR) or motion was aborted mid-residual.
 */
static bool slew_residual_correction(int64_t ra_dist, int64_t dec_dist,
                                     uint32_t total_ra_steps, uint32_t total_dec_steps,
                                     int ra_sign, int dec_sign, uint32_t spp)
{
    int ra_residual = (int)(ra_dist - (int64_t)total_ra_steps * (int64_t)spp);
    int dec_residual = (int)(dec_dist - (int64_t)total_dec_steps * (int64_t)spp);

    if (ra_residual == 0 && dec_residual == 0) {
        return true;   /* already exact */
    }

    /* Fine resolution: one STEP pulse == one canonical step. */
    if (!motors_microsteps_prepare(MOTION_CMD_TRACK, 0, 0)) {
        return false;   /* mount is in ERROR */
    }

    rmt_symbol_word_t sym;

    for (int i = 0; i < ra_residual; i++) {
        if (!s_motion.active) return false;
        int64_t next = motors_state.ra_steps + (int64_t)ra_sign;
        if (!motors_is_valid_ra_steps(next)) break;   /* limit — accept the residual */

        motors_rmt_encode_pulse(&sym);
        if (motors_rmt_transmit_ra(&sym, 1) != ESP_OK
            || motors_rmt_wait_ra(pdMS_TO_TICKS(2000)) != ESP_OK) {
            abort_motion();
            return false;
        }
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.ra_steps = next;
        portEXIT_CRITICAL(&motors_state_lock);
    }

    for (int i = 0; i < dec_residual; i++) {
        if (!s_motion.active) return false;
        int64_t next = motors_state.dec_steps + (int64_t)dec_sign;
        if (!motors_is_valid_dec_steps(next)) break;   /* limit — accept the residual */

        motors_rmt_encode_pulse(&sym);
        if (motors_rmt_transmit_dec(&sym, 1) != ESP_OK
            || motors_rmt_wait_dec(pdMS_TO_TICKS(2000)) != ESP_OK) {
            abort_motion();
            return false;
        }
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.dec_steps = next;
        portEXIT_CRITICAL(&motors_state_lock);
    }

    return true;
}

static void slewing_loop_rmt(void) {
    /* Total step counts — exact from int64_t targets. */
    int64_t ra_dist = s_motion.ra_target - s_motion.ra_start;
    int64_t dec_dist = s_motion.dec_target - s_motion.dec_start;
    if (ra_dist < 0) ra_dist = -ra_dist;
    if (dec_dist < 0) dec_dist = -dec_dist;

    /* The RMT loop schedules STEP pulses.  At the coarse microstep resolution
     * one pulse advances the axis by `spp` canonical steps, so total/remaining/
     * done are all kept in PULSES here; only the motors_state counter and the
     * limit headroom (in clamp_*_batch) use canonical units. */
    uint32_t spp = motors_steps_per_pulse();
    uint32_t total_ra_steps = (uint32_t) (ra_dist / (int64_t) spp);
    uint32_t total_dec_steps = (uint32_t) (dec_dist / (int64_t) spp);
    uint32_t ra_steps_done = 0;
    uint32_t dec_steps_done = 0;

    /* Ramp distances are compared in degrees, derived from the canonical
     * (resolution-independent) step distance. */
    int distance_ra_cds = (int)(motors_steps_to_deg(ra_dist) * 100.0f);
    int distance_dec_cds = (int)(motors_steps_to_deg(dec_dist) * 100.0f);

    /*
     * Direction is constant for a slew — set DIR pins once.
     */
    MotorDirection ra_dir = (s_motion.ra_target > motors_state.ra_steps)
                                ? MOTOR_DIRECTION_POSITIVE
                                : MOTOR_DIRECTION_NEGATIVE;
    MotorDirection dec_dir = (s_motion.dec_target > motors_state.dec_steps)
                                ? MOTOR_DIRECTION_POSITIVE
                                : MOTOR_DIRECTION_NEGATIVE;
    motors_hw_set_direction_ra(ra_dir);
    motors_hw_set_direction_dec(dec_dir);

    int ra_sign = (ra_dir == MOTOR_DIRECTION_POSITIVE) ? 1 : -1;
    int dec_sign = (dec_dir == MOTOR_DIRECTION_POSITIVE) ? 1 : -1;

    /*
     * Double-buffer per axis: ping = in-flight, pong = queued behind it.
     * Each axis advances independently, so a slow axis never stalls the
     * other.  Stack-allocated, DMA-safe on ESP32-S3.
     */
    rmt_symbol_word_t ra_buf[2][RMT_BUFFER_SYMBOLS];
    rmt_symbol_word_t dec_buf[2][RMT_BUFFER_SYMBOLS];
    uint32_t ra_num[2] = {0, 0};
    uint32_t dec_num[2] = {0, 0};
    uint32_t ra_batch[2] = {0, 0};
    uint32_t dec_batch[2] = {0, 0};
    int ra_ping = 0;   /* RA: currently transmitting */
    int ra_pong = 1;   /* RA: queued behind ping */
    int dec_ping = 0;  /* DEC: currently transmitting */
    int dec_pong = 1;  /* DEC: queued behind ping */

    uint32_t ra_period = MAX_STEP_PERIOD_TICKS;
    uint32_t dec_period = MAX_STEP_PERIOD_TICKS;
    int64_t last_ramp_recalc_us = 0;
    bool is_move_axis = (s_motion.active_cmd_type == MOTION_CMD_MOVE_AXIS);

    /* ── Compute + encode + submit first batch (buf[0]) ── */

    compute_slew_periods(is_move_axis, 0, 0,
                         (int64_t) total_ra_steps, (int64_t) total_dec_steps,
                         distance_ra_cds, distance_dec_cds,
                         &ra_period, &dec_period);
    last_ramp_recalc_us = esp_timer_get_time();

    uint32_t ra_rem = total_ra_steps - ra_steps_done;
    uint32_t dec_rem = total_dec_steps - dec_steps_done;
    ra_batch[0] = compute_batch_size(ra_period, ra_rem);
    dec_batch[0] = compute_batch_size(dec_period, dec_rem);
    ra_batch[0] = clamp_ra_batch(motors_state.ra_steps, ra_sign, ra_batch[0]);
    dec_batch[0] = clamp_dec_batch(motors_state.dec_steps, dec_sign, dec_batch[0]);

    /* Encode first batch. */
    int64_t ra_trav_base = (int64_t)ra_sign
                           * (motors_state.ra_steps - s_motion.ra_start) / (int64_t)spp;
    int64_t dec_trav_base = (int64_t)dec_sign
                            * (motors_state.dec_steps - s_motion.dec_start) / (int64_t)spp;

    ra_num[0] = encode_axis_batch(ra_buf[0], RMT_BUFFER_SYMBOLS,
                                   ra_batch[0], ra_period,
                                   motors_state.ra_speed,
                                   ra_trav_base, (int64_t) total_ra_steps, distance_ra_cds,
                                   is_move_axis);
    dec_num[0] = encode_axis_batch(dec_buf[0], RMT_BUFFER_SYMBOLS,
                                    dec_batch[0], dec_period,
                                    motors_state.dec_speed,
                                    dec_trav_base, (int64_t) total_dec_steps, distance_dec_cds,
                                    is_move_axis);

    /* Check stop before touching hardware. */
    if (!s_motion.active) return;

    /* Submit first batch (with semaphore drain). */
    esp_err_t tx_err = ESP_OK;
    if (ra_batch[0] > 0) tx_err = motors_rmt_transmit_ra(ra_buf[0], ra_num[0]);
    if (dec_batch[0] > 0 && tx_err == ESP_OK)
        tx_err = motors_rmt_transmit_dec(dec_buf[0], dec_num[0]);
    if (tx_err != ESP_OK) {
        ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(tx_err));
        abort_motion();
        return;
    }

    /*
     * Pre-encode AND pre-submit the second batch (buf[1]) while buf[0]
     * is transmitting.  This is the key: by the time buf[0] finishes,
     * buf[1] is already queued in the RMT TX FIFO and starts with
     * zero CPU-dependent gap.
     */
    {
        int64_t ra_proj = (int64_t) ra_steps_done + (int64_t) ra_batch[0];
        int64_t dec_proj = (int64_t) dec_steps_done + (int64_t) dec_batch[0];

        ra_rem = total_ra_steps - ra_steps_done - ra_batch[0];
        dec_rem = total_dec_steps - dec_steps_done - dec_batch[0];
        ra_batch[1] = compute_batch_size(ra_period, ra_rem);
        dec_batch[1] = compute_batch_size(dec_period, dec_rem);
        ra_batch[1] = clamp_ra_batch(motors_state.ra_steps + (int64_t)ra_sign * (int64_t)ra_batch[0] * (int64_t)spp,
                                     ra_sign, ra_batch[1]);
        dec_batch[1] = clamp_dec_batch(motors_state.dec_steps + (int64_t)dec_sign * (int64_t)dec_batch[0] * (int64_t)spp,
                                       dec_sign, dec_batch[1]);

        ra_num[1] = encode_axis_batch(ra_buf[1], RMT_BUFFER_SYMBOLS,
                                       ra_batch[1], ra_period,
                                       motors_state.ra_speed,
                                       ra_proj, (int64_t) total_ra_steps, distance_ra_cds,
                                       is_move_axis);
        dec_num[1] = encode_axis_batch(dec_buf[1], RMT_BUFFER_SYMBOLS,
                                        dec_batch[1], dec_period,
                                        motors_state.dec_speed,
                                        dec_proj, (int64_t) total_dec_steps, distance_dec_cds,
                                        is_move_axis);

        /* Submit buf[1] while buf[0] is still transmitting — ZERO GAP */
        if (ra_batch[1] > 0 || dec_batch[1] > 0) {
            tx_err = ESP_OK;
            if (ra_batch[1] > 0)
                tx_err = motors_rmt_transmit_no_drain_ra(ra_buf[1], ra_num[1]);
            if (dec_batch[1] > 0 && tx_err == ESP_OK)
                tx_err = motors_rmt_transmit_no_drain_dec(dec_buf[1], dec_num[1]);
            if (tx_err != ESP_OK) {
                ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(tx_err));
                abort_motion();
                return;
            }
        }
    }

    /* ── Main decoupled loop — each axis advances independently ──
     *
     * Invariant at loop entry:
     *   ra_buf[ra_ping] / dec_buf[dec_ping] — submitted, transmitting
     *   ra_buf[ra_pong] / dec_buf[dec_pong] — submitted, queued in TX FIFO
     *
     * Each iteration:
     *   1. recompute ramp periods (shared, throttled to 5 ms)
     *   2. advance an axis as soon as its in-flight batch completes —
     *      confirm position, encode + submit its next batch, swap its
     *      ping/pong — without waiting for the other axis
     *   3. check stop / conditions / finish
     *   4. yield briefly to avoid a busy-wait
     */

    while (s_motion.active) {
        /* A newer MoveAxis setpoint preempts the running one: abort now so
         * the main loop dequeues and applies it, instead of the continuous
         * move running to the mechanical limit first. */
        if (is_move_axis && move_axis_update_pending()) {
            s_motion.active = false;
            s_motion.aborted = true;
            break;
        }

        /*
         * 1. Ramp recalc — pick the next batch size from each axis's
         *    projected travelled (pong batch included).
         */
        int64_t now = esp_timer_get_time();
        if (now - last_ramp_recalc_us > 5000) {
            int64_t ra_proj_trav = (int64_t) ra_steps_done + (int64_t) ra_batch[ra_pong];
            int64_t dec_proj_trav = (int64_t) dec_steps_done + (int64_t) dec_batch[dec_pong];
            compute_slew_periods(is_move_axis,
                                 ra_proj_trav, dec_proj_trav,
                                 (int64_t) total_ra_steps, (int64_t) total_dec_steps,
                                 distance_ra_cds, distance_dec_cds,
                                 &ra_period, &dec_period);
            last_ramp_recalc_us = now;
        }

        /* 2a. RA — advance when its in-flight batch completes. */
        if (ra_steps_done < total_ra_steps
            && ra_batch[ra_ping] > 0
            && motors_rmt_try_wait_ra()) {
            /* Confirm + account the completed batch even when a stop is
             * requested: those steps were physically emitted, so counting them
             * keeps the position exact.  A graceful stop then drains the
             * remaining in-flight batch after this loop; an emergency abort
             * leaves it unaccounted and marks the position uncertain. */
            portENTER_CRITICAL(&motors_state_lock);
            motors_state.ra_steps += (int64_t)ra_sign * (int64_t)ra_batch[ra_ping] * (int64_t)spp;
            portEXIT_CRITICAL(&motors_state_lock);
            ra_steps_done += ra_batch[ra_ping];
            ra_batch[ra_ping] = 0;

            if (!s_motion.active) break;

            int64_t ra_proj_trav = (int64_t) ra_steps_done + (int64_t) ra_batch[ra_pong];
            ra_rem = total_ra_steps - ra_steps_done - ra_batch[ra_pong];
            ra_batch[ra_ping] = compute_batch_size(ra_period, ra_rem);
            ra_batch[ra_ping] = clamp_ra_batch(motors_state.ra_steps + (int64_t)ra_sign * (int64_t)ra_batch[ra_pong] * (int64_t)spp,
                                               ra_sign, ra_batch[ra_ping]);
            ra_num[ra_ping] = encode_axis_batch(ra_buf[ra_ping], RMT_BUFFER_SYMBOLS,
                                                 ra_batch[ra_ping], ra_period,
                                                 motors_state.ra_speed,
                                                 ra_proj_trav, (int64_t) total_ra_steps, distance_ra_cds,
                                                 is_move_axis);
            if (ra_batch[ra_ping] > 0) {
                tx_err = motors_rmt_transmit_no_drain_ra(ra_buf[ra_ping], ra_num[ra_ping]);
                if (tx_err != ESP_OK) {
                    ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(tx_err));
                    abort_motion();
                    break;
                }
            }
            { int t = ra_ping; ra_ping = ra_pong; ra_pong = t; }
        }

        /* 2b. DEC — advance when its in-flight batch completes. */
        if (dec_steps_done < total_dec_steps
            && dec_batch[dec_ping] > 0
            && motors_rmt_try_wait_dec()) {
            /* Confirm + account the completed batch even when a stop is
             * requested (see the RA comment above). */
            portENTER_CRITICAL(&motors_state_lock);
            motors_state.dec_steps += (int64_t)dec_sign * (int64_t)dec_batch[dec_ping] * (int64_t)spp;
            portEXIT_CRITICAL(&motors_state_lock);
            dec_steps_done += dec_batch[dec_ping];
            dec_batch[dec_ping] = 0;

            if (!s_motion.active) break;

            int64_t dec_proj_trav = (int64_t) dec_steps_done + (int64_t) dec_batch[dec_pong];
            dec_rem = total_dec_steps - dec_steps_done - dec_batch[dec_pong];
            dec_batch[dec_ping] = compute_batch_size(dec_period, dec_rem);
            dec_batch[dec_ping] = clamp_dec_batch(motors_state.dec_steps + (int64_t)dec_sign * (int64_t)dec_batch[dec_pong] * (int64_t)spp,
                                                  dec_sign, dec_batch[dec_ping]);
            dec_num[dec_ping] = encode_axis_batch(dec_buf[dec_ping], RMT_BUFFER_SYMBOLS,
                                                   dec_batch[dec_ping], dec_period,
                                                   motors_state.dec_speed,
                                                   dec_proj_trav, (int64_t) total_dec_steps, distance_dec_cds,
                                                   is_move_axis);
            if (dec_batch[dec_ping] > 0) {
                tx_err = motors_rmt_transmit_no_drain_dec(dec_buf[dec_ping], dec_num[dec_ping]);
                if (tx_err != ESP_OK) {
                    ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(tx_err));
                    abort_motion();
                    break;
                }
            }
            { int t = dec_ping; dec_ping = dec_pong; dec_pong = t; }
        }

        /* 3. Stop / conditions after any position updates. */
        if (!check_motion_conditions()) break;
        if (!s_motion.active) break;

        /* Target reached on both axes? */
        if (ra_steps_done >= total_ra_steps && dec_steps_done >= total_dec_steps) {
            if (slew_residual_correction(ra_dist, dec_dist, total_ra_steps, total_dec_steps,
                                         ra_sign, dec_sign, spp)) {
                finish_motion();
            }
            break;
        }

        /* 4. Yield — batch durations are far coarser than 1 ms, so this
         *    adds no meaningful latency and keeps the CPU free. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* ── Post-loop: graceful drain vs. emergency uncertainty ──
     *
     * A graceful stop (motors_stop / motors_park) leaves one or two batches
     * still streaming on the RMT: confirm + account them so the position
     * counters stay exact.  An emergency abort (power loss, hardware fault, or
     * a move-axis preempt) cuts the RMT immediately, so any batch not yet
     * confirmed is lost and the position becomes only approximately reliable —
     * flagged so the client can re-establish the origin instead of trusting a
     * drifted counter. */
    if (s_motion.graceful) {
        int64_t drain_deadline = esp_timer_get_time() + 500000;  /* 500 ms */
        while (esp_timer_get_time() < drain_deadline
               && (ra_batch[ra_ping] > 0 || ra_batch[ra_pong] > 0
                   || dec_batch[dec_ping] > 0 || dec_batch[dec_pong] > 0)) {
            bool accounted = false;

            /* RA: promote a queued batch to the in-flight slot, then confirm. */
            if (ra_batch[ra_ping] == 0 && ra_batch[ra_pong] > 0) {
                { int t = ra_ping; ra_ping = ra_pong; ra_pong = t; }
            } else if (ra_batch[ra_ping] > 0 && motors_rmt_try_wait_ra()) {
                portENTER_CRITICAL(&motors_state_lock);
                motors_state.ra_steps += (int64_t)ra_sign * (int64_t)ra_batch[ra_ping] * (int64_t)spp;
                portEXIT_CRITICAL(&motors_state_lock);
                ra_steps_done += ra_batch[ra_ping];
                ra_batch[ra_ping] = 0;
                { int t = ra_ping; ra_ping = ra_pong; ra_pong = t; }
                accounted = true;
            }

            /* DEC: same. */
            if (dec_batch[dec_ping] == 0 && dec_batch[dec_pong] > 0) {
                { int t = dec_ping; dec_ping = dec_pong; dec_pong = t; }
            } else if (dec_batch[dec_ping] > 0 && motors_rmt_try_wait_dec()) {
                portENTER_CRITICAL(&motors_state_lock);
                motors_state.dec_steps += (int64_t)dec_sign * (int64_t)dec_batch[dec_ping] * (int64_t)spp;
                portEXIT_CRITICAL(&motors_state_lock);
                dec_steps_done += dec_batch[dec_ping];
                dec_batch[dec_ping] = 0;
                { int t = dec_ping; dec_ping = dec_pong; dec_pong = t; }
                accounted = true;
            }

            if (!accounted) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        s_motion.graceful = false;

        /* Drain timed out — a committed batch never completed (RMT hang).
         * Fall back to an emergency abort so the position is flagged rather
         * than silently wrong. */
        if (ra_batch[ra_ping] > 0 || ra_batch[ra_pong] > 0
            || dec_batch[dec_ping] > 0 || dec_batch[dec_pong] > 0) {
            s_motion.aborted = true;
        }
    }

    /* Emergency abort with unconfirmed batches: the physical axes advanced
     * further than the counters reflect.  Mark the position uncertain. */
    if (s_motion.aborted
        && (ra_batch[ra_ping] > 0 || ra_batch[ra_pong] > 0
            || dec_batch[dec_ping] > 0 || dec_batch[dec_pong] > 0)) {
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.position_uncertain = true;
        portEXIT_CRITICAL(&motors_state_lock);
    }
}

/* --------------------------------------------------------------------------
 * Tracking motion loop — absolute-time fractional accumulator + RMT pulse.
 *
 * Designed for continuous open-ended tracking (sidereal, solar, lunar)
 * where timing precision must hold over arbitrarily long sessions.
 *
 * Scheduling strategy (hybrid sleep + fine-wait → RMT):
 *
 *   dt = now - last_time
 *   accumulator += dt / period_us     (period_us converted from RMT ticks)
 *   while (accumulator >= 1.0):  encode 1 step, rmt_transmit, wait, acc -= 1.0
 *
 *   deadline = now + (1.0 - accumulator) * period_us    (µs-exact)
 *   if deadline - now > 2 ms:
 *       vTaskDelay most of it (capped 50 ms, yields CPU → near-zero consumption)
 *   fine-wait remaining margin with busy-wait → µs precision
 *
 * The actual STEP pulse is generated by the RMT peripheral (hardware-timed
 * shape).  The fine-wait determines *when* the pulse begins; the RMT
 * determines the pulse *shape*.  Long idle periods (tracking ≈ 22 ms at
 * sidereal rate) are streamed as idle-only RMT symbols via DMA — the CPU
 * sleeps through the entire step period.
 *
 * Only RA is stepped during tracking; DEC velocity is always zero.
 * -------------------------------------------------------------------------- */
/*
 * Recompute the effective velocity for one axis, folding in any active
 * PulseGuide offset.  Returns the RMT step period in ticks.
 */
static void tracking_loop_rmt(void) {
    const int64_t FINE_MARGIN_US = 2000;
    const float deg_per_step = motors_get_deg_per_microstep();

    /* Signed phase accumulators in microsteps. */
    double ra_phase = 0.0;
    double dec_phase = 0.0;
    int64_t last_time_us = esp_timer_get_time();
    int64_t last_check_us = last_time_us;

    int ra_dir_set = 0;   /* 0 = unknown, ±1 = last set RA direction */
    int dec_dir_set = 0;
    rmt_symbol_word_t ra_sym[RMT_BUFFER_SYMBOLS];
    rmt_symbol_word_t dec_sym[RMT_BUFFER_SYMBOLS];

    /*
     * Effective velocities (deg/s).  Persist across iterations so the
     * phase integral at the top of the loop uses the velocity that was
     * actually in effect during the elapsed interval, before any guide
     * start/end events at `now` are applied.
     */
    float ra_vel = (motors_state.status == MOTORS_STATUS_TRACKING)
                   ? motors_state.ra_speed : 0.0f;
    float dec_vel = 0.0f;

    while (s_motion.active) {
        int64_t now = esp_timer_get_time();
        double dt_s = (double)(now - last_time_us) / 1000000.0;
        last_time_us = now;

        /*
         * Integrate the interval that just elapsed using the velocities
         * that were in effect during it (ra_vel/dec_vel hold the previous
         * iteration's values).  Guide start/end events at `now` are applied
         * after this, so each velocity covers exactly its own interval —
         * no retroactive guide motion.
         */
        ra_phase += (double)ra_vel * dt_s / (double)deg_per_step;
        dec_phase += (double)dec_vel * dt_s / (double)deg_per_step;

        /*
         * Drain queued guiding and tracking-mode updates while this loop is
         * active.  The task never returns to xQueueReceive() in the main loop
         * during tracking, so PHD2 corrections and tracking-mode changes would
         * otherwise sit unprocessed — and a pending TRACK at the front of the
         * queue would block the PulseGuide corrections queued behind it.
         * TRACK is idempotent here (re-applying the same mode is a no-op).
         * SLEW and MOVE_AXIS must not run inline: they stay queued and are
         * preceded by a STOP that ends this loop anyway.
         */
        {
            MotionCommand queued;
            while (xQueuePeek(motion_cmd_queue, &queued, 0) == pdTRUE) {
                if (queued.type == MOTION_CMD_PULSE_GUIDE
                    || queued.type == MOTION_CMD_TRACK) {
                    xQueueReceive(motion_cmd_queue, &queued, 0);
                    process_command(queued);
                    continue;
                }
                /* A full movement (SLEW / MOVE_AXIS) sits ahead of any guide
                 * corrections.  It cannot run inline, so stop tracking now and
                 * let the main loop dequeue and execute it — a PHD2 correction
                 * must never be left stranded behind a command that has to end
                 * this loop anyway. */
                s_motion.active = false;
                break;
            }
        }
        if (!s_motion.active) {
            break;   /* a full movement is pending — leave the tracking loop */
        }

        /* Guide expiry — checked every iteration for µs-precise pulse
         * termination instead of the previous 10 ms quantization. */
        if (s_motion.ra_guide_deadline_us
            && now >= s_motion.ra_guide_deadline_us) {
            s_motion.ra_guide_deadline_us = 0;
            s_motion.ra_guide_offset_dps = 0.0f;
        }
        if (s_motion.dec_guide_deadline_us
            && now >= s_motion.dec_guide_deadline_us) {
            s_motion.dec_guide_deadline_us = 0;
            s_motion.dec_guide_offset_dps = 0.0f;
            dec_dir_set = 0;
        }
        portENTER_CRITICAL(&motors_state_lock);
        motors_state.guiding = (s_motion.ra_guide_deadline_us != 0
                                || s_motion.dec_guide_deadline_us != 0);
        portEXIT_CRITICAL(&motors_state_lock);

        /* Velocities for the interval starting at `now`.  Standalone
         * guiding (status != TRACKING) has a zero base rate; only tracking
         * carries the continuous RA speed. */
        ra_vel = (motors_state.status == MOTORS_STATUS_TRACKING)
                 ? motors_state.ra_speed : 0.0f;
        if (s_motion.ra_guide_deadline_us)
            ra_vel += s_motion.ra_guide_offset_dps;
        dec_vel = 0.0f;
        if (s_motion.dec_guide_deadline_us)
            dec_vel = s_motion.dec_guide_offset_dps;

        /* Conditions check (every ~500 us). */
        if (now - last_check_us >= 500) {
            last_check_us = now;
            if (!check_motion_conditions()) break;
            if (motors_state.status != MOTORS_STATUS_TRACKING
                && !motors_state.guiding) {
                /* Standalone guide finished — clear the motion flag so
                 * idle state is unambiguous. */
                s_motion.active = false;
                break;
            }
        }

        /* ── Emit RA steps ─────────────────────────────────── */
        while (ra_phase >= 1.0) {
            if (ra_dir_set != 1) {
                motors_hw_set_direction_ra(MOTOR_DIRECTION_POSITIVE);
                ra_dir_set = 1;
            }
            if (!emit_ra_tracking_step(1, ra_sym)) {
                ra_phase = 0.0;
                return;
            }
            ra_phase -= 1.0;
        }
        while (ra_phase <= -1.0) {
            if (ra_dir_set != -1) {
                motors_hw_set_direction_ra(MOTOR_DIRECTION_NEGATIVE);
                ra_dir_set = -1;
            }
            if (!emit_ra_tracking_step(-1, ra_sym)) {
                ra_phase = 0.0;
                return;
            }
            ra_phase += 1.0;
        }

        /* ── Emit DEC steps ─────────────────────────────────── */
        while (dec_phase >= 1.0) {
            if (dec_dir_set != 1) {
                motors_hw_set_direction_dec(MOTOR_DIRECTION_POSITIVE);
                dec_dir_set = 1;
            }
            int64_t next = motors_state.dec_steps + 1;
            if (!motors_is_valid_dec_steps(next)) {
                ESP_LOGW(TAG, "DEC limit at %.3f deg",
                         (double)motors_steps_to_deg(motors_state.dec_steps));
                s_motion.dec_guide_deadline_us = 0;
                s_motion.dec_guide_offset_dps = 0.0f;
                dec_dir_set = 0;
                dec_phase = 0.0;
                break;
            }
            if (!s_motion.active) { dec_phase = 0.0; return; }

            motors_rmt_encode_pulse(dec_sym);
            esp_err_t tx_err = motors_rmt_transmit_dec(dec_sym, 1);
            if (tx_err != ESP_OK) {
                ESP_LOGE(TAG, "RMT DEC tx fail: %s", esp_err_to_name(tx_err));
                abort_motion();
                dec_phase = 0.0;
                break;
            }
            esp_err_t wait_err = motors_rmt_wait_dec(pdMS_TO_TICKS(2000));
            if (wait_err != ESP_OK) {
                ESP_LOGW(TAG, "RMT DEC wait %s",
                         (wait_err == ESP_ERR_TIMEOUT) ? "timeout" : "error");
                abort_motion();
                dec_phase = 0.0;
                break;
            }
            if (!s_motion.active) { dec_phase = 0.0; return; }

            portENTER_CRITICAL(&motors_state_lock);
            motors_state.dec_steps = next;
            portEXIT_CRITICAL(&motors_state_lock);
            dec_phase -= 1.0;
        }
        while (dec_phase <= -1.0) {
            if (dec_dir_set != -1) {
                motors_hw_set_direction_dec(MOTOR_DIRECTION_NEGATIVE);
                dec_dir_set = -1;
            }
            int64_t next = motors_state.dec_steps - 1;
            if (!motors_is_valid_dec_steps(next)) {
                ESP_LOGW(TAG, "DEC limit at %.3f deg",
                         (double)motors_steps_to_deg(motors_state.dec_steps));
                s_motion.dec_guide_deadline_us = 0;
                s_motion.dec_guide_offset_dps = 0.0f;
                dec_dir_set = 0;
                dec_phase = 0.0;
                break;
            }
            if (!s_motion.active) { dec_phase = 0.0; return; }

            motors_rmt_encode_pulse(dec_sym);
            esp_err_t tx_err = motors_rmt_transmit_dec(dec_sym, 1);
            if (tx_err != ESP_OK) {
                ESP_LOGE(TAG, "RMT DEC tx fail: %s", esp_err_to_name(tx_err));
                abort_motion();
                dec_phase = 0.0;
                break;
            }
            esp_err_t wait_err = motors_rmt_wait_dec(pdMS_TO_TICKS(2000));
            if (wait_err != ESP_OK) {
                ESP_LOGW(TAG, "RMT DEC wait %s",
                         (wait_err == ESP_ERR_TIMEOUT) ? "timeout" : "error");
                abort_motion();
                dec_phase = 0.0;
                break;
            }
            if (!s_motion.active) { dec_phase = 0.0; return; }

            portENTER_CRITICAL(&motors_state_lock);
            motors_state.dec_steps = next;
            portEXIT_CRITICAL(&motors_state_lock);
            dec_phase += 1.0;
        }

        /* ── Sleep ─────────────────────────────────────────── */
        /* Time to the next step boundary.  The next step is at
         * phase == +1 for positive velocity, at phase == -1 for
         * negative velocity, so the remaining phase depends on the
         * sign of the velocity. */
        double ra_remaining = (ra_vel > 0.0) ? (1.0 - ra_phase)
                                             : (1.0 + ra_phase);
        double dec_remaining = (dec_vel > 0.0) ? (1.0 - dec_phase)
                                               : (1.0 + dec_phase);
        double ra_s = (ra_vel != 0.0) ? ra_remaining * deg_per_step / fabs((double)ra_vel) : 1e9;
        double dec_s = (dec_vel != 0.0) ? dec_remaining * deg_per_step / fabs((double)dec_vel) : 1e9;
        int64_t w_us = (int64_t)((ra_s < dec_s ? ra_s : dec_s) * 1000000.0);
        if (w_us < 0) w_us = 0;

        /*
         * Wake at the earliest guide deadline so a pulse ends exactly on
         * time instead of rounding up to the next step period.  Fresh
         * clock read: step emission above may have consumed time since
         * `now` was captured at loop entry.
         */
        {
            int64_t now_us = esp_timer_get_time();
            if (s_motion.ra_guide_deadline_us) {
                int64_t until = s_motion.ra_guide_deadline_us - now_us;
                if (until < w_us) w_us = until;
            }
            if (s_motion.dec_guide_deadline_us) {
                int64_t until = s_motion.dec_guide_deadline_us - now_us;
                if (until < w_us) w_us = until;
            }
            if (w_us < 0) w_us = 0;
        }

        if (w_us > FINE_MARGIN_US) {
            uint32_t sm = (uint32_t)((w_us - FINE_MARGIN_US) / 1000);
            if (sm > 50) sm = 50;
            if (sm < 1) sm = 1;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sm));
            continue;
        }
        if (w_us > 0) {
            /* Block instead of busy-waiting.  At high guide rates the step
             * period (~187 us) is far below the 1 ms tick, so a busy-wait here
             * monopolizes the core and starves lower-priority tasks (the HTTP
             * server) for the whole pulse — PulseGuide responses then stall.
             * Blocking yields the CPU; the phase accumulator already integrates
             * the true elapsed time, so the coarser granularity only adds a
             * little step-timing jitter, never a cumulative rate error. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        }
    }
}

/* --------------------------------------------------------------------------
 * Motion loop dispatcher.
 *
 * Routes to the appropriate execution path based on mount status:
 *   - TRACKING (sidereal / solar / lunar) → tracking_loop_rmt()
 *     (absolute-time fractional accumulator, zero cumulative error)
 *   - Everything else (SLEWING, MOVE_AXIS, etc.) → slewing_loop_rmt()
 *     (batched RMT with ramps)
 * -------------------------------------------------------------------------- */
static void motion_loop(void) {
    /* Motor ERROR — refuse to enter any motion loop. */
    if (motors_status_is_error(motors_state.status)) {
        return;
    }

    /* Select the microstep resolution for this motion BEFORE any STEP pulse is
     * emitted.  The microstep module owns the policy (coarse for large ramped
     * slews, fine otherwise).  A failed switch stops the mount and puts it into
     * ERROR — abort here. */
    int64_t ra_dist = 0;
    int64_t dec_dist = 0;
    if (s_motion.active_cmd_type == MOTION_CMD_SLEW) {
        ra_dist = s_motion.ra_target - s_motion.ra_start;
        dec_dist = s_motion.dec_target - s_motion.dec_start;
    }
    if (!motors_microsteps_prepare(s_motion.active_cmd_type, ra_dist, dec_dist)) {
        return;
    }

    s_motion.aborted = false;
    s_motion.graceful = false;

    /* Standalone guide pulse (no tracking) — use the tracking loop
     * with zero base rate.  It handles DEC-only and RA-only guiding. */
    if ((motors_state.status == MOTORS_STATUS_TRACKING
         && motors_state.tracking != TRACKING_NONE)
        || motors_state.guiding) {
        tracking_loop_rmt();
    } else {
        slewing_loop_rmt();
    }

    /*
     * Resume tracking after a completed slew.  The mode is carried on the SLEW
     * command itself (not a separate TRACK enqueue), so a slew that pauses
     * tracking always restores it atomically on success and never leaves the
     * mount in an inconsistent stopped state.
     */
    if (s_motion.resume_tracking != TRACKING_NONE && !s_motion.aborted) {
        TrackingMode resume = s_motion.resume_tracking;
        s_motion.resume_tracking = TRACKING_NONE;

        /* A coarse slew left the drivers at MOTORS_MICROSTEPS_SLEW; tracking
         * runs at fine resolution, so restore it before the tracking loop. */
        if (!motors_microsteps_prepare(MOTION_CMD_TRACK, 0, 0)) {
            return;   /* mount is in ERROR */
        }

        portENTER_CRITICAL(&motors_state_lock);
        motors_state.ra_speed = motors_get_tracking_speed(resume);
        motors_state.dec_speed = 0.0f;
        motors_state.status = MOTORS_STATUS_TRACKING;
        motors_state.tracking = resume;
        portEXIT_CRITICAL(&motors_state_lock);

        /* RA always tracks positive at the sidereal/solar/lunar rates. */
        s_motion.ra_target = motors_deg_to_steps(motors_state.limits.ra_max);
        s_motion.dec_target = motors_state.dec_steps;
        s_motion.ra_start = motors_state.ra_steps;
        s_motion.dec_start = motors_state.dec_steps;
        s_motion.active = true;
        tracking_loop_rmt();
    }

    /*
     * Recreate the RMT channels only after an external abort (STOP / PARK /
     * preempt) — the one case where a queued double-buffered batch must be
     * dropped.  On normal completion every batch has finished, so a full
     * teardown + recreate is unnecessary and, done after every single move,
     * glitches the STEP output and can brown-out the board.
     */
    if (s_motion.aborted) {
        motors_rmt_reset_both();
    }
}

/* --------------------------------------------------------------------------
 * Motion task entry point.
 *
 * Blocks on the command queue when idle. When a motion-producing command
 * arrives (SLEW, TRACK, or MOVE_AXIS), enters motion_loop() which dispatches
 * to the appropriate RMT-driven execution path.
 * Stop / park are handled directly by their callers via
 * motors_motion_stop() + motors_state update.
 * -------------------------------------------------------------------------- */
static void motors_motion_task_run(void *arg) {
    (void) arg;

    /* Initialize RMT from this task — pins the RMT ISR to core 1
     * (isolated from everything else), eliminating ISR latency jitter. */
    esp_err_t rmt_err = motors_rmt_init();
    if (rmt_err != ESP_OK) {
        ESP_LOGE(TAG, "motors_rmt_init failed: %s — entering ERROR state",
                 esp_err_to_name(rmt_err));
        motors_enter_hardware_fault();
        motors_motion_task_handle = NULL;  /* clear the dangling handle */
        vTaskDelete(NULL);
        return;
    }
    while (true) {
        /* Signal idle so control ops (home/limits) can wait for the motion
         * to actually stop before touching counters/limits. */
        motors_motion_idle_give();
        motors_motion_process_one();
    }
}

/* --------------------------------------------------------------------------
 * Command processing — dequeue and execute one motion command.
 *
 * Extracted from the task loop so the host regression can drive the real
 * scheduler one command at a time with a simulated clock/RTOS/peripherals,
 * rather than re-implementing the step generator.  Runs the matching motion
 * loop, then recomputes `guiding` from any surviving guide deadlines.
 * -------------------------------------------------------------------------- */
void motors_motion_process_one(void) {
    MotionCommand cmd;
    if (xQueueReceive(motion_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
        return;
    }

    /* Consume the idle signal — the motion task is now running. */
    motors_motion_idle_take();

    process_command(cmd);

    if (cmd.type == MOTION_CMD_SLEW || cmd.type == MOTION_CMD_TRACK ||
        cmd.type == MOTION_CMD_MOVE_AXIS) {
        motion_loop();
    }
    /* PULSE_GUIDE: the tracking loop picks up guide state on each
     * iteration.  If tracking is NOT active, the guide pulse runs
     * via a minimal single-step loop below. */
    if (cmd.type == MOTION_CMD_PULSE_GUIDE
        && motors_state.status != MOTORS_STATUS_TRACKING) {
        /* Standalone guiding — run a short motion loop just for
         * the guide pulse.  DEC-only: no tracking base rate. */
        motion_loop();
    }

    /* Clear guiding flag when both axes are idle. */
    bool ra_active = (s_motion.ra_guide_deadline_us != 0)
                  && (esp_timer_get_time() < s_motion.ra_guide_deadline_us);
    bool dec_active = (s_motion.dec_guide_deadline_us != 0)
                   && (esp_timer_get_time() < s_motion.dec_guide_deadline_us);
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.guiding = (ra_active || dec_active);
    portEXIT_CRITICAL(&motors_state_lock);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t motors_motion_task_init(void) {
    if (motors_motion_idle_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create idle semaphore");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(
        motors_motion_task_run,
        "motors_motion",
        MOTION_TASK_STACK_BYTES,
        NULL,
        MOTION_TASK_PRIORITY,
        &motors_motion_task_handle,
        1) != pdPASS) {  /* dedicated core — CPU 1 is reserved for motion */
        ESP_LOGE(TAG, "Failed to create motion task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
