/* Motors - motors_limits.c
 *
 * Purpose: load, persist, and update operational axis limits.
 *
 * Limits are stored in NVS under the "mount" namespace alongside site
 * settings.  On first boot (no NVS) they default to the factory values
 * below.  The UI can override them at runtime; changes are committed
 * immediately and survive reboot.
 */
#include "motors.h"
#include "motors_internal.h"

#include "nvs.h"
#include "utils/utils.h"

#include "esp_log.h"

static const char *TAG = "MOTORS_LIMITS";
static const char *NVS_NS = "mount";

/* Hard physical travel limits (degrees): the mechanical range of both axes.
 * These bound the cable wrap and are NEVER exceeded — the operational (logical)
 * limits below can only be narrowed inside this range, never widened past it. */
#define PHYSICAL_RA_MIN   -100.0f
#define PHYSICAL_RA_MAX    100.0f
#define PHYSICAL_DEC_MIN  -135.0f
#define PHYSICAL_DEC_MAX    50.0f

/* Clamp a value to [lo, hi]. */
static float clamp_limit(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Re-clamp the four logical limits into the hard physical bounds.  Called after
 * every write (load, setters) so a runtime redefinition can never widen the
 * travel beyond the physical range. */
static void limits_clamp_to_physical(void) {
    motors_state.limits.ra_min  = clamp_limit(motors_state.limits.ra_min,  PHYSICAL_RA_MIN,  PHYSICAL_RA_MAX);
    motors_state.limits.ra_max  = clamp_limit(motors_state.limits.ra_max,  PHYSICAL_RA_MIN,  PHYSICAL_RA_MAX);
    motors_state.limits.dec_min = clamp_limit(motors_state.limits.dec_min, PHYSICAL_DEC_MIN, PHYSICAL_DEC_MAX);
    motors_state.limits.dec_max = clamp_limit(motors_state.limits.dec_max, PHYSICAL_DEC_MIN, PHYSICAL_DEC_MAX);
}

/* ── NVS persistence ─────────────────────────────────────── */

void motors_limits_load(void) {
    motors_state.limits.ra_min  = PHYSICAL_RA_MIN;
    motors_state.limits.ra_max  = PHYSICAL_RA_MAX;
    motors_state.limits.dec_min = PHYSICAL_DEC_MIN;
    motors_state.limits.dec_max = PHYSICAL_DEC_MAX;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS not found, using factory limits");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using factory limits: %s", esp_err_to_name(err));
        return;
    }

    uint32_t v;
    if (nvs_get_u32(h, "ra_min", &v) == ESP_OK)  motors_state.limits.ra_min  = uint32_to_float(v);
    if (nvs_get_u32(h, "ra_max", &v) == ESP_OK)  motors_state.limits.ra_max  = uint32_to_float(v);
    if (nvs_get_u32(h, "dec_min", &v) == ESP_OK) motors_state.limits.dec_min = uint32_to_float(v);
    if (nvs_get_u32(h, "dec_max", &v) == ESP_OK) motors_state.limits.dec_max = uint32_to_float(v);

    nvs_close(h);
    limits_clamp_to_physical();
}

void motors_limits_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u32(h, "ra_min",  float_to_uint32(motors_state.limits.ra_min));
    nvs_set_u32(h, "ra_max",  float_to_uint32(motors_state.limits.ra_max));
    nvs_set_u32(h, "dec_min", float_to_uint32(motors_state.limits.dec_min));
    nvs_set_u32(h, "dec_max", float_to_uint32(motors_state.limits.dec_max));

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

/* ── Runtime setters ─────────────────────────────────────── */

bool motors_set_current_as_home(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — home not redefined");
        return false;
    }
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.ra_steps  = 0;
    motors_state.dec_steps = 0;
    motors_state.status    = MOTORS_STATUS_READY;
    motors_state.position_uncertain = false;
    portEXIT_CRITICAL(&motors_state_lock);
    motors_limits_save();
    return true;
}

bool motors_set_current_as_ra_left_limit(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — RA left limit not set");
        return false;
    }
    float pos = motors_get_ra_deg();
    motors_state.limits.ra_min = pos;
    limits_clamp_to_physical();
    motors_limits_save();
    return true;
}

bool motors_set_current_as_ra_right_limit(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — RA right limit not set");
        return false;
    }
    float pos = motors_get_ra_deg();
    motors_state.limits.ra_max = pos;
    limits_clamp_to_physical();
    motors_limits_save();
    return true;
}

bool motors_set_current_as_dec_left_limit(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — DEC left limit not set");
        return false;
    }
    float pos = motors_get_dec_deg();
    motors_state.limits.dec_min = pos;
    limits_clamp_to_physical();
    motors_limits_save();
    return true;
}

bool motors_set_current_as_dec_right_limit(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — DEC right limit not set");
        return false;
    }
    float pos = motors_get_dec_deg();
    motors_state.limits.dec_max = pos;
    limits_clamp_to_physical();
    motors_limits_save();
    return true;
}

bool motors_limits_reset(void) {
    motors_stop();
    if (!motors_motion_wait_idle()) {
        ESP_LOGE(TAG, "Timed out waiting for idle — limits not reset");
        return false;
    }
    portENTER_CRITICAL(&motors_state_lock);
    motors_state.limits.ra_min  = PHYSICAL_RA_MIN;
    motors_state.limits.ra_max  = PHYSICAL_RA_MAX;
    motors_state.limits.dec_min = PHYSICAL_DEC_MIN;
    motors_state.limits.dec_max = PHYSICAL_DEC_MAX;
    portEXIT_CRITICAL(&motors_state_lock);
    motors_limits_save();
    return true;
}
