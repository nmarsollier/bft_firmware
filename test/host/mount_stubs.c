#include "mount/mount_internal.h"
#include <sys/time.h>

MountSettings mount_internal_state;
static MotorsState state;
static float dec_position;
static time_t clock_seconds;
int guide_axis;
float guide_offset;
uint32_t guide_duration;

void host_reset(time_t now) {
    clock_seconds = now;
    mount_internal_state = (MountSettings){.lat = -32.89f, .lon = 0};
    state = (MotorsState){.status = MOTORS_STATUS_READY,
        .limits = {-150, 150, -150, 150}};
    dec_position = 0;
    guide_axis = -1;
}
void host_limits(float ra_min, float ra_max, float dec_min, float dec_max) {
    state.limits.ra_min = ra_min; state.limits.ra_max = ra_max;
    state.limits.dec_min = dec_min; state.limits.dec_max = dec_max;
}
void host_dec(float dec) {
    dec_position = dec;
    /* Mirror the motion task: a non-zero DEC position re-derives the remembered
     * orientation from its sign. */
    if (dec != 0.0f) {
        state.dec_orientation_negative = (dec < 0.0f);
    }
}
void host_set_orientation(bool negative) {
    state.dec_orientation_negative = negative;
}
int host_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz; tv->tv_sec = clock_seconds; tv->tv_usec = 0; return 0;
}
MotorsState motors_current_state(void) { return state; }
float motors_get_dec_deg(void) { return dec_position; }
bool motors_status_is_error(MotorsStatus s) { return s == MOTORS_STATUS_ERROR; }
const char *motors_status_to_string(MotorsStatus s) { (void)s; return "ready"; }
MountResult mount_result_error(const char *msg) { return (MountResult){false, msg}; }
MountResult motors_result_code_error_result(MotorResultCode rc) {
    return (MountResult){rc == MOTOR_OK, ""};
}
MotorResultCode motors_pulse_guide_start(int axis, float offset, uint32_t duration) {
    guide_axis = axis; guide_offset = offset; guide_duration = duration;
    return MOTOR_OK;
}
/* Simplify the ABI used by the Python tests. */
bool host_guide(int direction) { return mount_pulse_guide(direction, 500).ok; }
