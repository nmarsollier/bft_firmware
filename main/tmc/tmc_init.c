/* TMC — tmc_init.c
 *
 * Purpose: initialise the two TMC2209 stepper drivers over their dedicated
 *          single-wire UART buses, and configure microstepping, current, and
 *          chopper mode on each.
 *
 * Each driver is addressed 0x00 (MS1 and MS2 tied to GND) on its own bus.  The
 * single-wire bus connects TX through a 1 kΩ series resistor to the TMC2209
 * PDN_UART pin, and RX directly to that pin; TX is floated while the driver's
 * response is read (see tmc_read_register).  No EN pin is connected — the
 * drivers are always enabled.
 */
#include "tmc.h"
#include "tmc_internal.h"
#include "config.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

/* ── UART hardware ─────────────────────────────────────────── */

#define TMC_BAUD_RATE    115200
#define TMC2209_ADDRESS  0x00   /* MS1=GND, MS2=GND */

/* ── TMC2209 register addresses ────────────────────────────── */

#define TMC_REG_GCONF      0x00
#define TMC_REG_IFCNT      0x02
#define TMC_REG_IOIN       0x06
#define TMC_REG_IHOLD_IRUN 0x10
#define TMC_REG_TPWMTHRS   0x13
#define TMC_REG_CHOPCONF   0x6C
#define TMC_REG_DRV_STATUS 0x6F
#define TMC_REG_PWMCONF    0x70

/* ── Target microstep resolution — see MOTORS_MICROSTEPS in config.h ── */

/* MOTORS_MICROSTEPS must be a valid TMC2209 microstep value; otherwise
 * tmc_microsteps_to_mres() silently falls through to its default. */
_Static_assert(MOTORS_MICROSTEPS == 256 || MOTORS_MICROSTEPS == 128 ||
               MOTORS_MICROSTEPS == 64  || MOTORS_MICROSTEPS == 32  ||
               MOTORS_MICROSTEPS == 16  || MOTORS_MICROSTEPS == 8   ||
               MOTORS_MICROSTEPS == 4   || MOTORS_MICROSTEPS == 2   ||
               MOTORS_MICROSTEPS == 1,
               "MOTORS_MICROSTEPS must be 256/128/64/32/16/8/4/2/1");

/* Convert MOTORS_MICROSTEPS to the MRES register field.
 * TMC2209 MRES mapping: 256→0, 128→1, 64→2, 32→3, 16→4, 8→5, 4→6, 2→7, 1→8 */
static uint8_t tmc_microsteps_to_mres(uint16_t ms) {
    switch (ms) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        default:  return 8;
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

static const char *TAG = "TMC_INIT";

/* ── Per-axis configuration ────────────────────────────────── */

typedef struct {
    const char *name;
    uart_port_t uart_num;
    gpio_num_t  tx_gpio;     /* TX → 1 kΩ → PDN_UART line */
    gpio_num_t  rx_gpio;     /* RX → PDN_UART line (direct) */
} TmcAxis;

static const TmcAxis tmc_axes[] = {
    { .name = "RA",  .uart_num = TMC_RA_UART_NUM,  .tx_gpio = TMC_RA_TX_GPIO,  .rx_gpio = TMC_RA_RX_GPIO },
    { .name = "DEC", .uart_num = TMC_DEC_UART_NUM, .tx_gpio = TMC_DEC_TX_GPIO, .rx_gpio = TMC_DEC_RX_GPIO },
};

#define TMC_AXIS_COUNT (sizeof(tmc_axes) / sizeof(tmc_axes[0]))

/* Per-axis UART readiness — set by tmc2209_hw_init() as each bus is installed. */
static bool s_uart_ok[TMC_AXIS_COUNT];

/* ── UART helpers ──────────────────────────────────────────── */

static uint8_t tmc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (((crc >> 7) ^ (byte & 0x01)) != 0) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
            byte >>= 1;
        }
    }
    return crc;
}

/*
 * Write a TMC2209 register over its dedicated single-wire UART bus.
 *
 * The written frame loops back into the RX FIFO (RX is on the same wire);
 * the echo is drained here so it never leaks into a subsequent read.
 */
static esp_err_t tmc_write_register(const TmcAxis *axis, uint8_t reg, uint32_t value)
{
    uint8_t request[8] = {
        0x05,
        TMC2209_ADDRESS,
        (uint8_t)(reg | 0x80),
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)(value),
        0x00,
    };
    request[7] = tmc_crc(request, 7);

    uart_flush_input(axis->uart_num);

    int written = uart_write_bytes(axis->uart_num, request, sizeof(request));
    if (written != (int)sizeof(request)) {
        return ESP_FAIL;
    }

    uart_wait_tx_done(axis->uart_num, pdMS_TO_TICKS(10));

    /* Absorb the 8-byte loopback echo from the single-wire bus. */
    uint8_t echo_buffer[8];
    uart_read_bytes(axis->uart_num, echo_buffer, sizeof(request), pdMS_TO_TICKS(10));

    return ESP_OK;
}

/*
 * Read a TMC2209 register over its dedicated single-wire UART bus.
 *
 * TX connects through a 1 kΩ series resistor to the TMC2209 PDN_UART
 * pin, and RX connects directly to that pin. After sending the read
 * request and draining the loopback echo, TX is floated (switched to
 * input) so the TMC2209 output can drive the line cleanly for its 8-byte
 * response — the internal pull-up holds the idle HIGH.
 *
 * Retries up to 6 attempts on timeout or invalid response — single-wire
 * UART reads are inherently prone to occasional corruption.
 */
static esp_err_t tmc_read_register(const TmcAxis *axis, uint8_t reg, uint32_t *value)
{
    if (value == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t request[4] = {
        0x05,
        TMC2209_ADDRESS,
        (uint8_t)(reg & 0x7F),   /* bit 7 = 0 → read */
        0x00,
    };
    request[3] = tmc_crc(request, 3);

    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 0; attempt < 6; attempt++) {
        if (attempt > 0)
            vTaskDelay(pdMS_TO_TICKS(10));

        uart_flush_input(axis->uart_num);

        int written = uart_write_bytes(axis->uart_num, request, sizeof(request));
        if (written != (int)sizeof(request)) return ESP_FAIL;

        uart_wait_tx_done(axis->uart_num, pdMS_TO_TICKS(10));

        /* Drain the 4-byte loopback echo before floating TX. */
        uint8_t echo_buf[4];
        uart_read_bytes(axis->uart_num, echo_buf, sizeof(request), pdMS_TO_TICKS(10));

        /*
         * Float TX so the TMC2209 output has a clean, high-impedance
         * line to drive for the remainder of its 8-byte response frame
         * (the internal pull-up holds the idle HIGH level).
         */
        gpio_set_direction(axis->tx_gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode(axis->tx_gpio, GPIO_PULLUP_ONLY);

        /* Collect the remaining bytes of the combined (echo + reply)
         * stream. Up to 32 bytes are buffered to tolerate a few extra
         * framing / noise bytes. */
        uint8_t response[32];
        int len = 0;
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50);

        while (xTaskGetTickCount() < deadline && len < (int)sizeof(response)) {
            int n = uart_read_bytes(axis->uart_num, response + len,
                                    sizeof(response) - len, pdMS_TO_TICKS(5));
            if (n > 0) len += n;
        }

        /* Restore TX — drive HIGH before re-enabling the output so the
         * bus doesn't glitch LOW while the IO MUX is reconnected. */
        gpio_set_level(axis->tx_gpio, 1);
        gpio_set_direction(axis->tx_gpio, GPIO_MODE_OUTPUT);
        uart_set_pin(axis->uart_num, axis->tx_gpio, axis->rx_gpio,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        if (len < 8) {
            last_err = ESP_ERR_TIMEOUT;
            continue;
        }

        /* Find and validate a frame in the response buffer. */
        for (int i = 0; i <= len - 8; i++) {
            uint8_t *frame = &response[i];
            if (frame[0] != 0x05) continue;
            if ((frame[2] & 0x7F) != (reg & 0x7F)) continue;
            if (tmc_crc(frame, 7) != frame[7]) continue;

            *value = ((uint32_t)frame[3] << 24) |
                     ((uint32_t)frame[4] << 16) |
                     ((uint32_t)frame[5] << 8)  |
                     ((uint32_t)frame[6]);
            return ESP_OK;
        }

        last_err = ESP_ERR_INVALID_RESPONSE;
    }

    return last_err;
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

static esp_err_t tmc_init_driver(const TmcAxis *axis)
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
    uint32_t chopconf = (10U << 0)   /* TOFF = 10 */
                      | (5U << 4)    /* HSTRT = 5 */
                      | (2U << 7)    /* HEND = 2 */
                      | (2U << 15)   /* TBL = 2 */
                      | ((uint32_t)tmc_microsteps_to_mres(MOTORS_MICROSTEPS) << 24) /* MRES */
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
     * bit 30 (stealth): 1 = StealthChop, 0 = SpreadCycle. With TPWMTHRS = 0
     * the driver should stay in StealthChop at any speed. CS_ACTUAL (bits
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

static void tmc_reset_diagnostics(void) {
    tmc_status = TMC_NOT_INIT;
    tmc_error = TMC_ERROR_NONE;
}

/* Configure and verify every driver's registers. Assumes the UARTs are already
 * installed and the buses have settled. */
static esp_err_t tmc_configure_drivers(void) {
    vTaskDelay(pdMS_TO_TICKS(20));

    bool all_ok = true;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!s_uart_ok[i]) {
            all_ok = false;  /* UART setup already failed for this axis */
            continue;
        }
        esp_err_t result = tmc_init_driver(&tmc_axes[i]);
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

    tmc2209_set_initialized(true);
    tmc_status = TMC_OK;
    tmc_error = TMC_ERROR_NONE;
    return ESP_OK;
}

/* ── Public entry points ───────────────────────────────────── */

esp_err_t tmc2209_hw_init(void)
{
    tmc_reset_diagnostics();

    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        s_uart_ok[i] = false;
    }

    /* Install and configure each driver's dedicated single-wire UART. */
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        const TmcAxis *axis = &tmc_axes[i];

        uart_config_t config = {
            .baud_rate  = TMC_BAUD_RATE,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t result = uart_driver_install(axis->uart_num, 512, 512, 0, NULL, 0);
        if (result != ESP_OK) {
            tmc_status = TMC_ERROR;
            tmc_error = TMC_ERROR_UART;
            ESP_LOGE(TAG, "%s: uart_driver_install failed: %s",
                     axis->name, esp_err_to_name(result));
            continue;
        }

        result = uart_param_config(axis->uart_num, &config);
        if (result != ESP_OK) {
            tmc_status = TMC_ERROR;
            tmc_error = TMC_ERROR_UART;
            ESP_LOGE(TAG, "%s: uart_param_config failed: %s",
                     axis->name, esp_err_to_name(result));
            continue;
        }

        /* Single-wire: TX through 1 kΩ, RX direct to the PDN_UART line. */
        result = uart_set_pin(axis->uart_num, axis->tx_gpio, axis->rx_gpio,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (result != ESP_OK) {
            tmc_status = TMC_ERROR;
            tmc_error = TMC_ERROR_UART;
            ESP_LOGE(TAG, "%s: uart_set_pin failed: %s",
                     axis->name, esp_err_to_name(result));
            continue;
        }

        gpio_set_pull_mode(axis->rx_gpio, GPIO_PULLUP_ONLY);
        s_uart_ok[i] = true;
    }

    return tmc_configure_drivers();
}

/* Probe both drivers: every driver must answer a register read for the mount
 * to be considered "TMC working". A silent driver means it is unpowered (the
 * driver is fed by VM) or faulty. */
bool tmc2209_probe(void)
{
    uint32_t value = 0;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!s_uart_ok[i]) {
            return false;
        }
        if (tmc_read_register(&tmc_axes[i], TMC_REG_DRV_STATUS, &value) != ESP_OK) {
            return false;
        }
    }
    return true;
}

/* Re-configure the driver registers after they stopped answering and then
 * answered again (power returned). */
esp_err_t tmc2209_reinit(void)
{
    tmc_reset_diagnostics();
    return tmc_configure_drivers();
}
