/* Mount - mount_pulse_guide.c
 *
 * Purpose: validate mount state for PulseGuide, map astronomical
 * directions (North/South/East/West) to signed RA/DEC speed offsets from the
 * current mechanical position, and delegate to the motors layer.
 *
 * Direction mapping (offset added to base tracking speed):
 *   East  → -RA  (increase α = decrease H = decrease R)
 *   West  → +RA
 *   North → increase δ; the mechanical DEC sign is the derivative of the
 *           conversion at the current position: dδ/dD = -p·sign(sin D),
 *           with p = +1 north / -1 south. It depends on the instantaneous
 *           geometry, never on remembered history.
 *   South → the opposite of North.
 */
#include "mount.h"
#include "mount_internal.h"

#include "motors.h"

#include <math.h>

#include "esp_log.h"

static const char *TAG = "MOUNT_PULSE_GUIDE";

MountResult mount_pulse_guide(GuideDirection direction, uint32_t duration_ms) {
    if (mount_is_error()) {
        return mount_result_error_state();
    }

    /* PulseGuide is only valid in READY (calibration) or TRACKING (guiding). */
    MotorsState state = motors_current_state();
    if (state.status != MOTORS_STATUS_READY &&
        state.status != MOTORS_STATUS_TRACKING) {
        ESP_LOGW(TAG, "Rejected: mount status=%s",
                 motors_status_to_string(state.status));
        return mount_result_error("Mount not ready for PulseGuide");
    }

    if (duration_ms == 0 || duration_ms > 30000) {
        return mount_result_error("Duration out of range (1-30000 ms)");
    }

    float ra_rate = mount_get_guide_rate_ra();
    float dec_rate = mount_get_guide_rate_dec();
    float p = (mount_internal_state.lat > 0.0f) ? 1.0f : -1.0f;

    MotorResultCode rc;
    switch (direction) {
    case GUIDE_DIRECTION_EAST:
        /* East moves the scope east relative to the sky: increase α, which is
         * -H = -R.  Mechanical RA offset is negative. */
        rc = motors_pulse_guide_start(0, -ra_rate, duration_ms);
        break;
    case GUIDE_DIRECTION_WEST:
        rc = motors_pulse_guide_start(0, ra_rate, duration_ms);
        break;
    case GUIDE_DIRECTION_NORTH:
    case GUIDE_DIRECTION_SOUTH: {
        /* North increases celestial δ. The mechanical DEC sign is dδ/dD at the
         * current position = -p·sign(sin D). */
        float dec_axis = motors_get_dec_deg();
        float sD = sinf(dec_axis * (float) M_PI / 180.0f);
        float north_sign = (sD >= 0.0f ? 1.0f : -1.0f) * (-p);
        float offset = (direction == GUIDE_DIRECTION_NORTH)
                       ? dec_rate * north_sign
                       : -dec_rate * north_sign;

        /* Reject a pulse whose signed DEC excursion reaches or crosses a
         * celestial pole (D = 180°·n), without executing any part of it. */
        float arc_deg = offset * (duration_ms / 1000.0f);
        float pole_eps = motors_steps_to_deg(1);   /* one microstep */
        float d_start = dec_axis;
        float d_end = dec_axis + arc_deg;
        float a = fminf(d_start, d_end) - pole_eps;
        float b = fmaxf(d_start, d_end) + pole_eps;
        int n_lo = (int) ceilf(a / 180.0f);
        int n_hi = (int) floorf(b / 180.0f);
        if (n_lo <= n_hi) {
            ESP_LOGW(TAG, "Rejected: DEC pulse crosses the celestial pole");
            return mount_result_error("Guide would cross the pole");
        }

        rc = motors_pulse_guide_start(1, offset, duration_ms);
        break;
    }
    default:
        return mount_result_error("Invalid guide direction");
    }

    return motors_result_code_error_result(rc);
}

/* ── Guide rate storage (deg/s) — per axis ── */

static float s_guide_rate_ra  = TRACKING_SPEED_SIDEREAL_DPS * 0.5f;
static float s_guide_rate_dec = TRACKING_SPEED_SIDEREAL_DPS * 0.5f;

void mount_set_guide_rate_ra(float rate_dps)  { s_guide_rate_ra = rate_dps; }
void mount_set_guide_rate_dec(float rate_dps) { s_guide_rate_dec = rate_dps; }
float mount_get_guide_rate_ra(void)           { return s_guide_rate_ra; }
float mount_get_guide_rate_dec(void)          { return s_guide_rate_dec; }
