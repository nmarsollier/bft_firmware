/* TMC — tmc_driver.c
 *
 * Purpose: full driver register configuration — the per-axis init sequence
 * (GCONF/IHOLD_IRUN/CHOPCONF/TPWMTHRS/PWMCONF) plus the shared
 * configure-all-drivers entry used by both tmc2209_hw_init() and
 * tmc2209_reinit().  Also owns the active microstep resolution state.
 */

#include "tmc.h"
#include "tmc_internal.h"
#include "config.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "TMC_DRIVER";

/* ── Target microstep resolution — see MOTORS_MICROSTEPS_TRACKING in config.h ── */

/* MOTORS_MICROSTEPS_TRACKING must be a valid TMC2209 microstep value; otherwise
 * tmc_microsteps_to_mres() returns false. */
_Static_assert(MOTORS_MICROSTEPS_TRACKING == 256 || MOTORS_MICROSTEPS_TRACKING == 128 ||
               MOTORS_MICROSTEPS_TRACKING == 64  || MOTORS_MICROSTEPS_TRACKING == 32  ||
               MOTORS_MICROSTEPS_TRACKING == 16  || MOTORS_MICROSTEPS_TRACKING == 8   ||
               MOTORS_MICROSTEPS_TRACKING == 4   || MOTORS_MICROSTEPS_TRACKING == 2   ||
               MOTORS_MICROSTEPS_TRACKING == 1,
               "MOTORS_MICROSTEPS_TRACKING must be 256/128/64/32/16/8/4/2/1");

_Static_assert(MOTORS_MICROSTEPS_SLEW == 256 || MOTORS_MICROSTEPS_SLEW == 128 ||
               MOTORS_MICROSTEPS_SLEW == 64  || MOTORS_MICROSTEPS_SLEW == 32  ||
               MOTORS_MICROSTEPS_SLEW == 16  || MOTORS_MICROSTEPS_SLEW == 8   ||
               MOTORS_MICROSTEPS_SLEW == 4   || MOTORS_MICROSTEPS_SLEW == 2   ||
               MOTORS_MICROSTEPS_SLEW == 1,
               "MOTORS_MICROSTEPS_SLEW must be 256/128/64/32/16/8/4/2/1");

/* Active microstep resolution on both drivers.  Only ever written after a
 * verified read-back (tmc2209_set_microsteps / tmc_configure_drivers), so it is
 * always the real hardware value — the motors layer trusts it via
 * tmc2209_get_microsteps(). */
uint16_t tmc_microsteps = MOTORS_MICROSTEPS_TRACKING;

/* ── MRES conversion ───────────────────────────────────────── */

/* Convert a microstep count to the MRES register field.
 * TMC2209 MRES mapping: 256→0, 128→1, 64→2, 32→3, 16→4, 8→5, 4→6, 2→7, 1→8 */
bool tmc_microsteps_to_mres(uint16_t ms, uint8_t *mres) {
    switch (ms) {
        case 256: *mres = 0; return true;
        case 128: *mres = 1; return true;
        case 64:  *mres = 2; return true;
        case 32:  *mres = 3; return true;
        case 16:  *mres = 4; return true;
        case 8:   *mres = 5; return true;
        case 4:   *mres = 6; return true;
        case 2:   *mres = 7; return true;
        case 1:   *mres = 8; return true;
        default:  return false;
    }
}

/* Convert MRES register field back to microsteps — used for read-back verification. */
static bool tmc_mres_to_microsteps(uint8_t mres, uint16_t *microsteps) {
    switch (mres) {
        case 0:  *microsteps = 256; return true;
        case 1:  *microsteps = 128; return true;
        case 2:  *microsteps = 64;  return true;
        case 3:  *microsteps = 32;  return true;
        case 4:  *microsteps = 16;  return true;
        case 5:  *microsteps = 8;   return true;
        case 6:  *microsteps = 4;   return true;
        case 7:  *microsteps = 2;   return true;
        case 8:  *microsteps = 1;   return true;
        default: return false;
    }
}

/* ── Verified write helpers ─────────────────────────────────── */

/*
 * Write a register and verify the hardware latched it by reading it back.
 * Only the bits set in `mask` must match — reserved bits that the hardware
 * ignores are simply excluded. Retries on flaky single-wire UART reads.
 */
static esp_err_t tmc_write_verified(const TmcAxis *axis, uint8_t reg,
                                    uint32_t value, uint32_t mask,
                                    uint32_t *readback)
{
    uint32_t rv = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (tmc_write_register(axis, reg, value) != ESP_OK) {
            return ESP_FAIL;
        }
        if (tmc_read_register(axis, reg, &rv) == ESP_OK
            && (rv & mask) == (value & mask)) {
            if (readback != NULL) *readback = rv;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (readback != NULL) *readback = rv;
    return ESP_FAIL;
}

/*
 * Write a write-only register (IHOLD_IRUN, TPWMTHRS) and verify the driver
 * accepted it: IFCNT increments on every valid write. Write-only registers
 * cannot be read back, so IFCNT is the only available proof of acceptance.
 */
static esp_err_t tmc_write_only_verified(const TmcAxis *axis, uint8_t reg,
                                         uint32_t value)
{
    uint32_t before = 0, after = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (tmc_read_register(axis, TMC_REG_IFCNT, &before) == ESP_OK
            && tmc_write_register(axis, reg, value) == ESP_OK
            && tmc_read_register(axis, TMC_REG_IFCNT, &after) == ESP_OK
            && ((after - before) & 0xFFu) == 1u) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return ESP_FAIL;
}

/* ── Driver init (write + verify) ──────────────────────────── */

static esp_err_t tmc_init_driver(const TmcAxis *axis, uint16_t microsteps)
{
    esp_err_t result;
    uint32_t verify = 0;

    ESP_LOGI(TAG, "--- Initialising axis %s [UART %d, TX %d, RX %d] ---",
             axis->name, axis->uart_num, axis->tx_gpio, axis->rx_gpio);

    /* ── GCONF: StealthChop base (silent below the TPWMTHRS threshold) ──
     * The SPREAD pin inverts en_SpreadCycle (spread_forzado = en_SpreadCycle
     * XOR SPREAD), so read its level from IOIN and set en_SpreadCycle (bit 2)
     * to the SAME value — this neutralises the inversion and keeps the base
     * mode StealthChop whatever the board's SPREAD wiring is.  With TPWMTHRS
     * > 0 the driver then falls back to SpreadCycle above the speed threshold
     * (smoother, more torque at high speed).
     * mstep_reg_select (bit 7) = 1 takes microsteps from CHOPCONF.MRES;
     * pdn_disable (bit 6) = 1 keeps PDN_UART as a pure UART. */
    uint32_t ioin = 0;
    bool spread_high = false;
    if (tmc_read_register(axis, TMC_REG_IOIN, &ioin) == ESP_OK) {
        spread_high = (ioin & (1U << 8)) != 0;   /* IOIN[8] = SPREAD pin level */
    }
    uint32_t gconf = 0x000000C0 | (spread_high ? (1U << 2) : 0);
    uint32_t gconf_mask = (1U << 7) | (1U << 6) | (1U << 2);
    result = tmc_write_verified(axis, TMC_REG_GCONF, gconf, gconf_mask, &verify);
    if (result != ESP_OK) {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_GCONF_WRITE;
        ESP_LOGE(TAG, "%s: GCONF write/verify failed (0x%08lX)", axis->name, (unsigned long)verify);
        return result;
    }
    ESP_LOGI(TAG, "%s: GCONF verified — StealthChop forced (SPREAD=%s) (0x%08lX)",
             axis->name, spread_high ? "HIGH" : "LOW", (unsigned long)verify);

    /* ── IHOLD_IRUN (write-only) ──
     * Field layout: IHOLD [4:0], IRUN [12:8], IHOLDDELAY [19:16]. */
    uint32_t ihold_irun = (uint32_t)TMC_IHOLD       /* IHOLD [4:0] */
                        | ((uint32_t)TMC_IRUN << 8) /* IRUN  [12:8] */
                        | (1U << 16);               /* IHOLDDELAY = 1 */
    result = tmc_write_only_verified(axis, TMC_REG_IHOLD_IRUN, ihold_irun);
    if (result != ESP_OK) {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_IHOLD_WRITE;
        ESP_LOGE(TAG, "%s: IHOLD_IRUN write not accepted", axis->name);
        return result;
    }

    /* ── CHOPCONF (read-write) ── */
    uint8_t mres = 0;
    (void) tmc_microsteps_to_mres(microsteps, &mres);  /* validated by _Static_assert */
    uint32_t chopconf = (10U << 0)   /* TOFF = 10 */
                      | (5U << 4)    /* HSTRT = 5 */
                      | (2U << 7)    /* HEND = 2 */
                      | (2U << 15)   /* TBL = 2 */
                      | ((uint32_t) mres << 24) /* MRES */
                      | (1U << 28);  /* INTPOL → 256 µsteps */
    uint32_t chopconf_mask = 0x0FU | (0x7U << 4) | (0xFU << 7)
                           | (0x3U << 15) | (1U << 17)
                           | (0xFU << 24) | (1U << 28);
    result = tmc_write_verified(axis, TMC_REG_CHOPCONF, chopconf, chopconf_mask, &verify);
    if (result != ESP_OK) {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_CHOPCONF_VERIFY;
        ESP_LOGE(TAG, "%s: CHOPCONF write/verify failed (0x%08lX)", axis->name, (unsigned long)verify);
        return result;
    }

    uint16_t verified_msteps = 0;
    tmc_mres_to_microsteps((uint8_t)((verify >> 24) & 0x0F), &verified_msteps);

    ESP_LOGI(TAG, "%s: CHOPCONF verified — %u µsteps (0x%08lX)",
             axis->name, verified_msteps, (unsigned long)verify);

    /* ── TPWMTHRS (write-only): speed threshold for StealthChop → SpreadCycle ── */
    result = tmc_write_only_verified(axis, TMC_REG_TPWMTHRS, TMC_TPWMTHRS);
    if (result != ESP_OK) {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_TPWMTHRS_WRITE;
        ESP_LOGE(TAG, "%s: TPWMTHRS write not accepted", axis->name);
        return result;
    }

    /* ── PWMCONF: silent StealthChop PWM ──
     * PWM_AUTOSCALE + PWM_AUTOGRAD auto-tune the current regulation for the
     * smoothest, quietest drive; PWM_FREQ = 1 → 35 kHz (inaudible). */
    uint32_t pwmconf = (36U << 0)     /* PWM_OFS = 36 (default) */
                     | (1U << 16)     /* PWM_FREQ = 1 → 35 kHz */
                     | (1U << 18)     /* PWM_AUTOSCALE */
                     | (1U << 19)     /* PWM_AUTOGRAD */
                     | (8U << 24)     /* PWM_REG = 8 (smooth regulation) */
                     | (12U << 28);   /* PWM_LIM = 12 (default) */
    uint32_t pwmconf_mask = 0xFFU | (0xFFU << 8) | (0x3U << 16)
                          | (1U << 18) | (1U << 19)
                          | (0x3U << 20) | (0xFU << 24) | (0xFU << 28);
    result = tmc_write_verified(axis, TMC_REG_PWMCONF, pwmconf, pwmconf_mask, &verify);
    if (result != ESP_OK) {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_PWMCONF_VERIFY;
        ESP_LOGE(TAG, "%s: PWMCONF write/verify failed (0x%08lX)", axis->name, (unsigned long)verify);
        return result;
    }
    ESP_LOGI(TAG, "%s: PWMCONF verified — StealthChop auto-scale (0x%08lX)", axis->name, (unsigned long)verify);

    /* ── DRV_STATUS: confirm the chopper mode and cross-check IHOLD ──
     * bit 30 (stealth): 1 = StealthChop, 0 = SpreadCycle. CS_ACTUAL (bits
     * 20:16) reflects IHOLD at standstill, cross-checking the IHOLD_IRUN write. */
    if (tmc_read_register(axis, TMC_REG_DRV_STATUS, &verify) == ESP_OK) {
        bool stealth = (verify & (1U << 30)) != 0;
        uint8_t cs_actual = (uint8_t)((verify >> 16) & 0x1F);
        ESP_LOGI(TAG, "%s: DRV_STATUS=0x%08lX — %s (CS_ACTUAL=%u, IHOLD=%u)",
                 axis->name, (unsigned long)verify,
                 stealth ? "StealthChop" : "SpreadCycle", cs_actual, (unsigned)TMC_IHOLD);
        if (!stealth) {
            ESP_LOGW(TAG, "%s: StealthChop expected at standstill — check SPREAD pin (HIGH inverts en_SpreadCycle)",
                     axis->name);
        }
    } else {
        ESP_LOGW(TAG, "%s: DRV_STATUS read failed — chopper mode unknown", axis->name);
    }

    ESP_LOGI(TAG, "%s: init complete (µsteps=%u, irun=%u, ihold=%u)",
             axis->name, verified_msteps, (unsigned)TMC_IRUN, (unsigned)TMC_IHOLD);
    return ESP_OK;
}

/* ── Shared init helpers ───────────────────────────────────── */

void tmc_reset_diagnostics(void) {
    tmc_status = TMC_NOT_INIT;
    tmc_error = TMC_ERROR_NONE;
}

/* Configure and verify every driver's registers. Assumes the UARTs are already
 * installed and the buses have settled. */
esp_err_t tmc_configure_drivers(void) {
    vTaskDelay(pdMS_TO_TICKS(20));

    bool all_ok = true;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!tmc_uart_ok[i]) {
            all_ok = false;  /* UART setup already failed for this axis */
            continue;
        }
        esp_err_t result = tmc_init_driver(&tmc_axes[i], MOTORS_MICROSTEPS_TRACKING);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Error initialising axis %s", tmc_axes[i].name);
            all_ok = false;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (!all_ok) {
        ESP_LOGE(TAG, "Driver failed UART verification — microsteps not trusted");
        tmc2209_set_initialized(false);
        return ESP_FAIL;
    }

    /* same value the drivers were just verified with */
    tmc_microsteps = MOTORS_MICROSTEPS_TRACKING;
    tmc2209_set_initialized(true);
    tmc_status = TMC_OK;
    tmc_error = TMC_ERROR_NONE;
    return ESP_OK;
}
