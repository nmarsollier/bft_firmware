/* tmc_internal.h — internal API shared among TMC module files
 *
 * Functions and variables exposed here are for use ONLY within the
 * main/tmc/ module. No file outside tmc/ should include this header.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tmc.h"

#include "driver/gpio.h"
#include "driver/uart.h"

/* ── Transport constants ───────────────────────────────────── */

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

/* ── Per-axis table (tmc_uart.c) ───────────────────────────── */

typedef struct {
    const char *name;
    uart_port_t uart_num;
    gpio_num_t  tx_gpio;     /* TX → 1 kΩ → PDN_UART line */
    gpio_num_t  rx_gpio;     /* RX → PDN_UART line (direct) */
} TmcAxis;

#define TMC_AXIS_COUNT 2
extern const TmcAxis tmc_axes[TMC_AXIS_COUNT];

/* Per-axis UART readiness — set by tmc2209_hw_init(). */
extern bool tmc_uart_ok[TMC_AXIS_COUNT];

/* ── UART access serialisation (tmc_uart.c) ────────────────── */

esp_err_t tmc_uart_lock(void);
void tmc_uart_unlock(void);

/* ── UART register primitives (tmc_uart.c) ─────────────────── */

esp_err_t tmc_write_register(const TmcAxis *axis, uint8_t reg, uint32_t value);
esp_err_t tmc_read_register(const TmcAxis *axis, uint8_t reg, uint32_t *value);

/* ── MRES conversion (tmc_driver.c) ────────────────────────── */

bool tmc_microsteps_to_mres(uint16_t ms, uint8_t *mres);

/* ── Driver configuration — shared by hw_init and reinit (tmc_driver.c) ── */

void tmc_reset_diagnostics(void);
esp_err_t tmc_configure_drivers(void);

/* Active microstep resolution — written by tmc_configure_drivers() and
 * tmc2209_set_microsteps(), read by tmc2209_get_microsteps() (tmc_driver.c). */
extern uint16_t tmc_microsteps;

/* ── Init diagnostics (tmc_status.c) ───────────────────────── */

extern TmcStatus tmc_status;
extern TmcError  tmc_error;

/* ── Init state (tmc_get_initialized.c) ────────────────────── */

void tmc2209_set_initialized(bool initialized);
