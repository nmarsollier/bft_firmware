/* TMC — tmc_probe.c
 *
 * Purpose: probe both drivers over UART — return true only when both answer a
 * register read.  This is the "TMC working" sense: the drivers are fed by VM
 * (12 V), so they are silent when the rail is absent and their logic supply
 * collapses.
 */

#include "tmc.h"
#include "tmc_internal.h"

#include "esp_err.h"

bool tmc2209_probe(void)
{
    if (tmc_uart_lock() != ESP_OK) {
        return false;
    }

    bool ok = true;
    uint32_t value = 0;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!tmc_uart_ok[i]
            || tmc_read_register(&tmc_axes[i], TMC_REG_DRV_STATUS, &value) != ESP_OK) {
            ok = false;
            break;
        }
    }

    tmc_uart_unlock();
    return ok;
}
