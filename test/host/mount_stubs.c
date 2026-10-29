#include "mount/mount_internal.h"
#include <sys/time.h>

MountSettings mount_internal_state;
bool mount_time_valid = true;

static MotorsState state;
static float dec_position;
static float ra_position;
static time_t clock_seconds;
int guide_axis;
float guide_offset;
uint32_t guide_duration;

/* Last slew recorded by motors_slew_to_angle. */
int slew_called;
float slew_ra;
float slew_dec;

void host_reset(time_t now) {
    clock_seconds = now;
    mount_internal_state = (MountSettings){.lat = -32.89f, .lon = 0};
    state = (MotorsState){.status = MOTORS_STATUS_READY,
        .limits = {-150, 150, -150, 150}};
    dec_position = 0;
    ra_position = 0;
    guide_axis = -1;
    slew_called = 0;
}
void host_lat(float lat) {
    mount_internal_state.lat = lat;
}
void host_limits(float ra_min, float ra_max, float dec_min, float dec_max) {
    state.limits.ra_min = ra_min; state.limits.ra_max = ra_max;
    state.limits.dec_min = dec_min; state.limits.dec_max = dec_max;
}
void host_dec(float dec) {
    dec_position = dec;
}
void host_ra(float ra) {
    ra_position = ra;
}
void host_time(double t) {
    clock_seconds = (time_t) t;
}
int host_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz; tv->tv_sec = clock_seconds; tv->tv_usec = 0; return 0;
}
MotorsState motors_current_state(void) { return state; }
float motors_get_dec_deg(void) { return dec_position; }
float motors_get_ra_deg(void) { return ra_position; }
float motors_steps_to_deg(int64_t steps) {
    return (float) steps * (1.8f / (32.0f * 500.0f));
}
int64_t motors_deg_to_steps(float degrees) {
    float deg_per_step = 1.8f / (32.0f * 500.0f);
    return (int64_t)(degrees / deg_per_step + (degrees >= 0.0f ? 0.5f : -0.5f));
}
float motors_get_slewing_speed(int speed_rate) {
    switch (speed_rate) {
        case 1: return 0.5f;
        case 2: return 1.0f;
        case 3: return 2.5f;
        default: return 4.0f;
    }
}
bool motors_status_is_error(MotorsStatus s) { return s == MOTORS_STATUS_ERROR; }
const char *motors_status_to_string(MotorsStatus s) { (void)s; return "ready"; }
MountResult mount_result_error(const char *msg) { return (MountResult){false, msg}; }
MountResult mount_result_ok(void) { return (MountResult){true, "OK"}; }
MountResult motors_result_code_error_result(MotorResultCode rc) {
    return (MountResult){rc == MOTOR_OK, ""};
}
MotorResultCode motors_slew_to_angle(float ra_deg, float dec_deg, int speed_rate) {
    (void) speed_rate;
    slew_called = 1;
    slew_ra = ra_deg;
    slew_dec = dec_deg;
    return MOTOR_OK;
}
MotorResultCode motors_pulse_guide_start(int axis, float offset, uint32_t duration) {
    guide_axis = axis; guide_offset = offset; guide_duration = duration;
    return MOTOR_OK;
}
/* Simplify the ABI used by the Python tests. */
bool host_guide(int direction) { return mount_pulse_guide(direction, 500).ok; }
bool host_slew(float ra, float dec, int speed) {
    return mount_slew_to_coordinates(ra, dec, speed).ok;
}
