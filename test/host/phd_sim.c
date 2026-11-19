/* phd_sim.c — host harness for the PHD2 PulseGuide regression.
 *
 * Compiles the REAL production guide path:
 *   mount_pulse_guide.c   (direction -> axis/offset mapping, pole rejection)
 *   motors_pulse_guide.c  (PULSE_GUIDE enqueue)
 *   motors_command_queue.c(thread-safe queue)
 *   motors_rmt.c          (step pulse encoding + transmit orchestration)
 *   motors_hw.c           (DIR GPIO)
 *   motors_current_state.c(step <-> degree accessors)
 *
 * Only the clock, RTOS primitives and peripherals are simulated.  The RMT
 * "transmit" is observed: every STEP transmission is recorded per axis with its
 * direction and pulse shape, and the position counter only advances when the
 * transmission actually succeeds — mirroring production, where an RMT TX error
 * aborts the pulse.  A "suppress DEC RMT" mutation makes the nominal DEC pulse
 * emit zero steps, so a motionless DEC is detected even though every layer
 * above returns ok=true.
 */

#include "mount/mount.h"
#include "mount/mount_internal.h"
#include "motors/motors.h"
#include "motors/motors_internal.h"

#include "freertos/semphr.h"
#include "esp_timer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── motors module globals (normally defined in motors_init.c) ──
 * motors_motion_task_handle and motors_stop_generation are defined by the real
 * motors_motion_task.c, which this harness now compiles. */

MotorsState motors_state;
portMUX_TYPE motors_state_lock = portMUX_INITIALIZER_UNLOCKED;

/* ── mount module globals (normally defined in mount_init.c) ── */

MountSettings mount_internal_state;
bool mount_time_valid = false;

/* ── FreeRTOS queue (ring buffer) ── */

typedef struct host_queue {
    void *items;
    size_t item_size;
    int length;
    int head, tail, count;
} host_queue_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    host_queue_t *q = calloc(1, sizeof(host_queue_t));
    q->items = malloc((size_t)length * item_size);
    q->item_size = item_size;
    q->length = (int)length;
    return q;
}

BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t wait) {
    (void)wait;
    if (q->count >= q->length) return pdFALSE;
    memcpy((char *)q->items + (size_t)q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->length;
    q->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) {
    (void)wait;
    if (q->count == 0) return pdFALSE;
    memcpy(item, (char *)q->items + (size_t)q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count--;
    return pdTRUE;
}

BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait) {
    (void)wait;
    if (q->count == 0) return pdFALSE;
    memcpy(item, (char *)q->items + (size_t)q->head * q->item_size, q->item_size);
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q) {
    q->head = q->tail = q->count = 0;
    return pdTRUE;
}

void vQueueDelete(QueueHandle_t q) { free(q->items); free(q); }

/* ── FreeRTOS semaphore (counting) ── */

typedef struct host_semaphore { int count; int max; } host_semaphore_t;

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial) {
    host_semaphore_t *s = calloc(1, sizeof(host_semaphore_t));
    s->count = (int)initial;
    s->max = (int)max;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateCounting(1, 0); }

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    if (sem->count < sem->max) { sem->count++; return pdTRUE; }
    return pdFALSE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken) {
    if (woken) *woken = pdFALSE;
    return xSemaphoreGive(sem);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait) {
    (void)wait;
    if (sem->count > 0) { sem->count--; return pdTRUE; }
    return pdFALSE;
}

void vSemaphoreDelete(SemaphoreHandle_t sem) { free(sem); }

/* ── virtual monotonic clock (advanced by ulTaskNotifyTake / vTaskDelay) ── */

/* Deferred-RMT flush helpers — defined after the RMT channel struct, but the
 * virtual-clock functions below call them to fire in-flight TX-done events. */
static void sim_fire_done(int axis);
static void sim_flush(int axis, int64_t now);
static void sim_flush_all(int64_t now);

static int64_t g_virtual_time_us = 0;

/* Sequence injection: a schedule of guide pulses injected during continuous
 * tracking.  Each entry is enqueued when the virtual clock reaches at_ms; the
 * motion loop is stopped at g_run_budget_us.  Drives the REAL scheduler. */
typedef struct {
    int direction;
    uint32_t duration_ms;
    uint32_t at_ms;      /* virtual ms from sequence start */
} guide_injection_t;

static guide_injection_t g_schedule[64];
static int g_schedule_count = 0;
static int g_schedule_idx = 0;
static int64_t g_run_budget_us = INT64_MAX;

int64_t esp_timer_get_time(void) {
    /* Simulate µs-scale real-time passage between calls: when the tracking loop
     * computes a zero-length sleep (the next step is due "now") it must still
     * advance the virtual clock, otherwise the loop spins forever. */
    g_virtual_time_us += 1;
    sim_flush_all(g_virtual_time_us);
    return g_virtual_time_us;
}

/* ── FreeRTOS task / notify (no scheduler — driven explicitly) ── */

BaseType_t xTaskNotify(TaskHandle_t t, uint32_t v, eNotifyAction a) {
    (void)t; (void)v; (void)a; return pdTRUE;
}

BaseType_t xTaskNotifyGive(TaskHandle_t t) { (void)t; return pdTRUE; }

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    (void)clear;
    g_virtual_time_us += (int64_t)wait * 1000;   /* ticks == ms */
    sim_flush_all(g_virtual_time_us);
    while (g_schedule_idx < g_schedule_count &&
           g_virtual_time_us >= (int64_t)g_schedule[g_schedule_idx].at_ms * 1000) {
        mount_pulse_guide((GuideDirection)g_schedule[g_schedule_idx].direction,
                          g_schedule[g_schedule_idx].duration_ms);
        g_schedule_idx++;
    }
    if (g_virtual_time_us >= g_run_budget_us) {
        motors_motion_stop();
    }
    return 0;
}

void vTaskDelay(TickType_t ticks) {
    g_virtual_time_us += (int64_t)ticks * 1000;
    sim_flush_all(g_virtual_time_us);
}
void vTaskDelete(TaskHandle_t t) { (void)t; }

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
                                   uint32_t stack, void *arg, uint32_t prio,
                                   TaskHandle_t *out, int core) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)out; (void)core;
    return pdFAIL;
}

/* ── GPIO ── */

static int g_dir_ra = -1;   /* last RA DIR level (0/1), -1 = unset */
static int g_dir_dec = -1;  /* last DEC DIR level (0/1), -1 = unset */

esp_err_t gpio_config(const gpio_config_t *cfg) { (void)cfg; return ESP_OK; }

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level) {
    if (gpio == RA_DIR_GPIO) g_dir_ra = (int)level;
    if (gpio == DEC_DIR_GPIO) g_dir_dec = (int)level;
    return ESP_OK;
}

/* ── esp_rom / esp_err ── */

void esp_rom_delay_us(uint32_t us) { (void)us; }

const char *esp_err_to_name(esp_err_t err) {
    switch (err) {
        case ESP_OK: return "OK";
        case ESP_FAIL: return "FAIL";
        case ESP_ERR_NO_MEM: return "NO_MEM";
        case ESP_ERR_INVALID_STATE: return "INVALID_STATE";
        case ESP_ERR_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}

/* ── RMT driver simulation ── */

typedef struct host_rmt_channel {
    int gpio;
    rmt_tx_done_callback_t on_done;
    void *user;
    int enabled;
} host_rmt_channel_t;

typedef struct host_rmt_encoder { int _unused; } host_rmt_encoder_t;

static host_rmt_channel_t g_channels[2];

/* Per-axis observation record. */
typedef struct {
    int count;            /* number of transmissions */
    int total_symbols;    /* symbols == steps for guide pulses */
    int last_pulse_ticks; /* duration0 of the last first symbol (STEP_PULSE_TICKS) */
    int last_dir;         /* DIR level at the last transmission */
} rmt_obs_t;

static rmt_obs_t g_rmt[2];   /* [0]=RA, [1]=DEC */
static int g_suppress_dec = 0;

/* ── deferred-RMT mode (in-flight modelling for motion-abort tests) ──
 * Default 0 keeps the original immediate-TX-done behaviour so the PHD2 guide
 * tests are unchanged.  When 1, rmt_transmit enqueues the transmission and the
 * TX-done interrupt fires only after the virtual clock advances past the
 * batch's symbol duration — reproducing a real in-flight batch that a STOP can
 * interrupt mid-emission. */
static int g_defer_done = 0;
static int64_t g_phys_emitted[2];  /* steps physically emitted (oracle) */
static int g_stop_arm_kind = -1;   /* -1 none, 0 graceful (motors_stop), 1 emergency (motors_motion_stop) */
static int g_stop_arm_after = 0;   /* arm the stop after this many rmt_transmit calls */
static int g_tx_count = 0;

typedef struct {
    int num_steps;
    int64_t deadline_us;
} sim_pending_t;

#define SIM_PENDING_MAX 8
typedef struct {
    sim_pending_t items[SIM_PENDING_MAX];
    int head, tail, count;
} sim_pending_fifo_t;

static sim_pending_fifo_t g_pending[2];

static int channel_axis(rmt_channel_handle_t ch) {
    return ch->gpio == DEC_STEP_GPIO ? 1 : 0;
}

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *cfg, rmt_channel_handle_t *out) {
    /* Two fixed slots, one per axis.  Recreation after an abort reuses the same
     * slot so repeated teardowns never walk past the array. */
    int axis = (cfg->gpio_num == DEC_STEP_GPIO) ? 1 : 0;
    host_rmt_channel_t *ch = &g_channels[axis];
    memset(ch, 0, sizeof(*ch));
    ch->gpio = cfg->gpio_num;
    *out = ch;
    return ESP_OK;
}

esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *cfg, rmt_encoder_handle_t *out) {
    (void)cfg;
    static host_rmt_encoder_t enc;
    *out = &enc;
    return ESP_OK;
}

esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t ch,
                                          const rmt_tx_event_callbacks_t *cbs,
                                          void *user) {
    ch->on_done = cbs->on_trans_done;
    ch->user = user;
    return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t ch) { ch->enabled = 1; return ESP_OK; }
esp_err_t rmt_disable(rmt_channel_handle_t ch) { ch->enabled = 0; return ESP_OK; }
esp_err_t rmt_del_channel(rmt_channel_handle_t ch) { (void)ch; return ESP_OK; }
esp_err_t rmt_del_encoder(rmt_encoder_handle_t enc) { (void)enc; return ESP_OK; }
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t ch, int timeout_ms) {
    (void)timeout_ms;
    if (g_defer_done) {
        /* Emergency abort: the in-flight batch completes (wait_all_done) but the
         * motion loop has already broken, so its TX-done token is orphaned — the
         * steps are physically emitted yet never counted by the firmware. */
        int axis = channel_axis(ch);
        sim_pending_fifo_t *f = &g_pending[axis];
        while (f->count > 0) {
            /* Oracle counts canonical steps (pulses × active spp) so it stays
             * comparable to motors_state.ra_steps at either microstep resolution. */
            g_phys_emitted[axis] += (int64_t) f->items[f->head].num_steps
                                    * (int64_t) motors_steps_per_pulse();
            f->head = (f->head + 1) % SIM_PENDING_MAX;
            f->count--;
            sim_fire_done(axis);
        }
        return ESP_OK;
    }
    return ESP_OK;
}

/* Count STEP pulses (symbols with level0==1) and total emission time in us. */
static void sim_decode(const rmt_symbol_word_t *syms, int num, int *steps, int64_t *dur_us) {
    *steps = 0;
    int64_t ticks = 0;
    for (int i = 0; i < num; i++) {
        if (syms[i].level0 == 1) (*steps)++;
        ticks += (int64_t)syms[i].duration0 + (int64_t)syms[i].duration1;
    }
    *dur_us = ticks / 2;   /* 2 MHz → 1 tick = 0.5 us */
}

static void sim_pending_push(int axis, int steps, int64_t deadline_us) {
    sim_pending_fifo_t *f = &g_pending[axis];
    if (f->count >= SIM_PENDING_MAX) return;   /* cannot happen for ≤2 in flight */
    f->items[f->tail] = (sim_pending_t){ steps, deadline_us };
    f->tail = (f->tail + 1) % SIM_PENDING_MAX;
    f->count++;
}

static void sim_fire_done(int axis) {
    host_rmt_channel_t *ch = &g_channels[axis];
    if (ch->on_done) {
        rmt_tx_done_event_data_t edata = { .num_symbols = 0 };
        ch->on_done(ch, &edata, ch->user);
    }
}

/* Emit every transmission whose deadline has passed, firing TX-done per batch. */
static void sim_flush(int axis, int64_t now) {
    sim_pending_fifo_t *f = &g_pending[axis];
    while (f->count > 0 && f->items[f->head].deadline_us <= now) {
        g_phys_emitted[axis] += (int64_t) f->items[f->head].num_steps
                                * (int64_t) motors_steps_per_pulse();
        f->head = (f->head + 1) % SIM_PENDING_MAX;
        f->count--;
        sim_fire_done(axis);
    }
}

static void sim_flush_all(int64_t now) {
    sim_flush(0, now);
    sim_flush(1, now);
}

static void sim_maybe_arm_stop(void) {
    g_tx_count++;
    if (g_stop_arm_kind < 0 || g_tx_count < g_stop_arm_after) return;
    int kind = g_stop_arm_kind;
    g_stop_arm_kind = -1;   /* one-shot */
    if (kind == 0) motors_stop();          /* graceful */
    else motors_motion_stop();             /* emergency */
}

esp_err_t rmt_transmit(rmt_channel_handle_t ch, rmt_encoder_handle_t enc,
                       const void *payload, size_t payload_bytes,
                       const rmt_transmit_config_t *cfg) {
    (void)enc; (void)cfg;
    int axis = channel_axis(ch);

    /* Mutation: a suppressed DEC channel fails every transmission, so no step
     * is ever completed — the nominal DEC test must then fail. */
    if (axis == 1 && g_suppress_dec) {
        return ESP_FAIL;
    }

    const rmt_symbol_word_t *syms = (const rmt_symbol_word_t *)payload;
    int num = (int)(payload_bytes / sizeof(rmt_symbol_word_t));

    rmt_obs_t *r = &g_rmt[axis];
    r->count++;
    r->total_symbols += num;
    if (num > 0) r->last_pulse_ticks = (int)syms[0].duration0;
    r->last_dir = (axis == 0) ? g_dir_ra : g_dir_dec;

    if (g_defer_done) {
        /* In-flight model: the batch streams over its symbol duration and the
         * TX-done ISR fires only when the virtual clock reaches the deadline. */
        int steps; int64_t dur_us;
        sim_decode(syms, num, &steps, &dur_us);
        sim_pending_push(axis, steps, g_virtual_time_us + dur_us);
        sim_maybe_arm_stop();
        return ESP_OK;
    }

    /* Simulate the TX-done ISR firing immediately (gives the done semaphore). */
    if (ch->on_done) {
        rmt_tx_done_event_data_t edata = { .num_symbols = num };
        ch->on_done(ch, &edata, ch->user);
    }
    return ESP_OK;
}

/* ── motors/mount helpers not compiled from production ── */

MountResult mount_result_ok(void) { return (MountResult){ .ok = true, .message = "OK" }; }
MountResult mount_result_error(const char *message) {
    return (MountResult){ .ok = false, .message = message };
}

MountResult motors_result_code_error_result(MotorResultCode rc) {
    if (rc == MOTOR_OK) return mount_result_ok();
    return mount_result_error("motor error");
}

const char *motors_status_to_string(MotorsStatus status) {
    switch (status) {
        case MOTORS_STATUS_READY: return "ready";
        case MOTORS_STATUS_SLEWING: return "slewing";
        case MOTORS_STATUS_TRACKING: return "tracking";
        case MOTORS_STATUS_PARKED: return "parked";
        default: return "error";
    }
}

bool motors_status_is_error(MotorsStatus status) { return status == MOTORS_STATUS_ERROR; }
bool motors_status_is_parked(MotorsStatus status) { return status == MOTORS_STATUS_PARKED; }

bool motors_is_valid_ra(float value) {
    return value >= motors_state.limits.ra_min && value <= motors_state.limits.ra_max;
}
bool motors_is_valid_dec(float value) {
    return value >= motors_state.limits.dec_min && value <= motors_state.limits.dec_max;
}
bool motors_is_valid_ra_steps(int64_t steps) { return motors_is_valid_ra(motors_steps_to_deg(steps)); }
bool motors_is_valid_dec_steps(int64_t steps) { return motors_is_valid_dec(motors_steps_to_deg(steps)); }

/* ── public harness API (called via ctypes) ── */

typedef struct {
    int axis;             /* 0=RA, 1=DEC, -1=rejected/not dequeued */
    int ok;               /* mount_pulse_guide accepted */
    int status;           /* motors_state.status after the operation */
    int guiding;          /* motors_state.guiding after the operation */
    float offset_dps;     /* signed guide offset the mapping produced */
    int emitted_steps;    /* steps actually transmitted on the target axis */
    int64_t ra_steps;     /* final counters */
    int64_t dec_steps;
} sim_guide_result_t;

void sim_reset(void) {
    g_suppress_dec = 0;
    g_dir_ra = g_dir_dec = -1;
    g_virtual_time_us = 0;
    g_schedule_count = 0;
    g_schedule_idx = 0;
    g_run_budget_us = INT64_MAX;
    memset(g_rmt, 0, sizeof(g_rmt));
    g_defer_done = 0;
    g_stop_arm_kind = -1;
    g_stop_arm_after = 0;
    g_tx_count = 0;
    memset(g_phys_emitted, 0, sizeof(g_phys_emitted));
    memset(g_pending, 0, sizeof(g_pending));

    if (motion_cmd_queue == NULL) {
        motors_queue_init();
    }
    /* Drop any command left over from a previous sequence run (a guide injected
     * right at the budget stop may not have been drained yet). */
    motors_queue_clear();
    motors_rmt_init();
    motors_motion_task_handle = (TaskHandle_t)0x1;

    motors_state = (MotorsState){
        .ra_steps = 0,
        .dec_steps = motors_deg_to_steps(90.0f),   /* D = +90°, away from pole/limits */
        .status = MOTORS_STATUS_READY,
        .tracking = TRACKING_NONE,
        .limits = { .ra_min = -120.0f, .ra_max = 120.0f,
                    .dec_min = -50.0f, .dec_max = 150.0f },
    };
    mount_internal_state = (MountSettings){ .lat = -32.89f, .lon = -68.83f, .elevation = 750 };

    /* Reset guide rates to the documented default (0.5 × sidereal). */
    mount_set_guide_rate_ra(TRACKING_SPEED_SIDEREAL_DPS * 0.5f);
    mount_set_guide_rate_dec(TRACKING_SPEED_SIDEREAL_DPS * 0.5f);
}

void sim_set_lat(float lat) { mount_internal_state.lat = lat; }
void sim_set_dec_deg(float deg) { motors_state.dec_steps = motors_deg_to_steps(deg); }
void sim_suppress_dec(int on) { g_suppress_dec = on; }
int sim_set_guide_rate_ra(float rate) { return mount_set_guide_rate_ra(rate) ? 1 : 0; }
int sim_set_guide_rate_dec(float rate) { return mount_set_guide_rate_dec(rate) ? 1 : 0; }
int sim_status(void) { return (int)motors_state.status; }

sim_guide_result_t sim_guide(int direction, uint32_t duration_ms) {
    sim_guide_result_t r;
    memset(&r, 0, sizeof(r));
    r.axis = -1;
    memset(g_rmt, 0, sizeof(g_rmt));

    int64_t dec0 = motors_state.dec_steps;
    int64_t ra0 = motors_state.ra_steps;

    MountResult mr = mount_pulse_guide((GuideDirection)direction, duration_ms);
    r.ok = mr.ok ? 1 : 0;
    r.status = (int)motors_state.status;
    if (!mr.ok) return r;

    /* Peek the queued command to report the mapping (axis + signed offset). */
    MotionCommand cmd;
    if (xQueuePeek(motion_cmd_queue, &cmd, 0) == pdTRUE && cmd.type == MOTION_CMD_PULSE_GUIDE) {
        r.axis = cmd.guide_axis;
        r.offset_dps = cmd.guide_offset_dps;
    }

    /* Run the REAL scheduler: dequeue + process_command + motion_loop. */
    motors_motion_process_one();

    r.ra_steps = motors_state.ra_steps;
    r.dec_steps = motors_state.dec_steps;
    r.status = (int)motors_state.status;
    r.guiding = motors_state.guiding ? 1 : 0;
    r.emitted_steps = (int)((r.axis == 0) ? (r.ra_steps - ra0) : (r.dec_steps - dec0));
    return r;
}

int sim_rmt_symbols(int axis) { return g_rmt[axis].total_symbols; }
int sim_rmt_count(int axis) { return g_rmt[axis].count; }
int sim_rmt_pulse_ticks(int axis) { return g_rmt[axis].last_pulse_ticks; }
int sim_rmt_dir(int axis) { return g_rmt[axis].last_dir; }

/* ── motion-abort harness ── */

/* 1 = model in-flight RMT batches (deferred TX-done); 0 = immediate (default). */
void sim_defer_done(int on) { g_defer_done = on; }

/* Arm a one-shot stop after `after` more rmt_transmit calls.
 * kind 0 = graceful (motors_stop), kind 1 = emergency (motors_motion_stop). */
void sim_arm_stop(int kind, int after) { g_stop_arm_kind = kind; g_stop_arm_after = after; }

/* Steps physically emitted by the simulated RMT (independent oracle). */
int64_t sim_phys_emitted(int axis) { return g_phys_emitted[axis]; }

/* Firmware's position-uncertain flag. */
int sim_position_uncertain(void) { return motors_state.position_uncertain ? 1 : 0; }

/* ── wait-idle / limits harness ── */

/* Create the motion task's idle semaphore (task creation is stubbed away).
 * Leaves the semaphore count 0 — i.e. the motion task appears "busy" — so a
 * wait-idle call times out. */
void sim_init_motion_task(void) { motors_motion_task_init(); }

void sim_set_ra_deg(float deg) { motors_state.ra_steps = motors_deg_to_steps(deg); }

/* Enqueue a single-axis RA slew (DEC target = current DEC, so DEC is idle). */
void sim_enqueue_slew_ra(float ra_deg) {
    MotionCommand cmd = {
        .type = MOTION_CMD_SLEW,
        .ra_target_deg = ra_deg,
        .dec_target_deg = motors_steps_to_deg(motors_state.dec_steps),
        .ra_speed = 4.0f,
        .dec_speed = 4.0f,
    };
    motors_queue_put(&cmd);
}

/* Enqueue a raw DEC PulseGuide directly (bypassing mount_pulse_guide), to
 * exercise the motion task's execution-time pole-crossing revalidation. */
void sim_enqueue_raw_dec_guide(float offset_dps, uint32_t duration_ms) {
    MotionCommand cmd = {
        .type = MOTION_CMD_PULSE_GUIDE,
        .guide_axis = 1,   /* DEC */
        .guide_offset_dps = offset_dps,
        .guide_duration_ms = duration_ms,
    };
    motors_queue_put(&cmd);
}

/* ── continuous tracking + guide sequence ── */

int64_t sim_ra_steps(void) { return motors_state.ra_steps; }
int64_t sim_dec_steps(void) { return motors_state.dec_steps; }
int sim_guiding(void) { return motors_state.guiding ? 1 : 0; }

/* Enqueue a guide without running the scheduler (for pending-cancellation). */
int sim_enqueue_guide(int direction, uint32_t duration_ms) {
    return mount_pulse_guide((GuideDirection)direction, duration_ms).ok ? 1 : 0;
}

/* Low-level STOP: bump the stop generation and clear guides (not the queue). */
void sim_motion_stop(void) { motors_motion_stop(); }

/* Process one command through the real scheduler. */
void sim_process_one(void) { motors_motion_process_one(); }

/* Run the REAL scheduler with sidereal tracking for `total_ms` virtual ms,
 * injecting the scheduled guide pulses at their `at_ms` times.  The tracking
 * loop drains each injected guide inline, so DEC/RA advance as in production. */
void sim_run_tracking_sequence(guide_injection_t *sched, int count, uint32_t total_ms) {
    /* Injection times and the run budget are ABSOLUTE on the virtual clock, so
     * consecutive sequences (N then S) keep advancing the same clock instead of
     * restarting it. */
    uint32_t t0_ms = (uint32_t)(g_virtual_time_us / 1000);
    for (int i = 0; i < count; i++) {
        g_schedule[i] = sched[i];
        g_schedule[i].at_ms = t0_ms + sched[i].at_ms;
    }
    g_schedule_count = count;
    g_schedule_idx = 0;
    g_run_budget_us = g_virtual_time_us + (int64_t)total_ms * 1000;

    MotionCommand cmd = {
        .type = MOTION_CMD_TRACK,
        .ra_speed = TRACKING_SPEED_SIDEREAL_DPS,
        .dec_speed = 0.0f,
        .tracking_mode = TRACKING_SIDEREAL,
    };
    motors_queue_put(&cmd);

    motors_motion_process_one();

    g_schedule_count = 0;
    g_run_budget_us = INT64_MAX;
}

void sim_run_tracking_guide(int direction, uint32_t duration_ms,
                            uint32_t inject_at_ms, uint32_t total_ms) {
    guide_injection_t s = { direction, duration_ms, inject_at_ms };
    sim_run_tracking_sequence(&s, 1, total_ms);
}
