/* TMC — tmc_reinit.c
 *
 * Purpose: re-configure the driver registers after they stopped answering and
 * then answered again (power returned) — their registers were lost while
 * unpowered, so they are rewritten.  Does not re-install the UARTs.
 */

#include "tmc.h"
#include "tmc_internal.h"

#include "esp_err.h"

esp_err_t tmc2209_reinit(void)
{
    if (tmc_uart_lock() != ESP_OK) {
        return ESP_FAIL;
    }
    tmc_reset_diagnostics();
    esp_err_t result = tmc_configure_drivers();
    tmc_uart_unlock();
    return result;
}
