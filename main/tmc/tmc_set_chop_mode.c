/* TMC — tmc_set_chop_mode.c
 *
 * Purpose: change the chopper mode on both drivers at runtime — read-modify-write
 * of GCONF.en_SpreadCycle (accounting for the SPREAD pin inversion), verified by
 * read-back with retries.  StealthChop for tracking/guiding/gentle slews,
 * SpreadCycle for coarse slews.
 */

#include "tmc.h"
#include "tmc_internal.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "TMC_SET_CHOP_MODE";

/*
 * Read-modify-write GCONF.en_SpreadCycle (bit 2) so the *effective* mode matches
 * `spread_cycle`.  The SPREAD pin inverts the bit (spread_forzado = en_SpreadCycle
 * XOR SPREAD), so the bit is set to !SPREAD for SpreadCycle and to SPREAD for
 * StealthChop.  The other GCONF bits (pdn_disable, mstep_reg_select) are
 * preserved by the read-modify-write.  Retries 3 times before giving up.
 */
static esp_err_t tmc_set_gconf_spread(const TmcAxis *axis, bool spread_cycle) {
    bool spread_high = false;
    if (!tmc_read_spread_pin(axis, &spread_high)) {
        return ESP_FAIL;
    }
    bool bit2 = spread_cycle ? !spread_high : spread_high;

    uint32_t gconf = 0;
    if (tmc_read_register(axis, TMC_REG_GCONF, &gconf) != ESP_OK) {
        return ESP_FAIL;
    }
    uint32_t next = (gconf & ~(1U << 2)) | ((uint32_t) bit2 << 2);

    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t readback = 0;
        if (tmc_write_register(axis, TMC_REG_GCONF, next) == ESP_OK
            && tmc_read_register(axis, TMC_REG_GCONF, &readback) == ESP_OK
            && (((readback >> 2) & 1U) == (uint32_t) bit2)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return ESP_FAIL;
}

esp_err_t tmc2209_set_chop_mode(TmcChopMode mode)
{
    if (mode != TMC_CHOP_STEALTH && mode != TMC_CHOP_SPREAD) {
        ESP_LOGE(TAG, "invalid chop mode %d", (int) mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (tmc_uart_lock() != ESP_OK) {
        return ESP_FAIL;
    }

    bool spread_cycle = (mode == TMC_CHOP_SPREAD);
    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!tmc_uart_ok[i]) {
            result = ESP_FAIL;
            break;
        }
        if (tmc_set_gconf_spread(&tmc_axes[i], spread_cycle) != ESP_OK) {
            result = ESP_FAIL;
            break;
        }
    }

    if (result == ESP_OK) {
        tmc_chop_mode = mode;   /* verified on both axes */
        ESP_LOGI(TAG, "chop mode set to %s on both axes",
                 spread_cycle ? "SpreadCycle" : "StealthChop");
    } else {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_CHOP_MODE_SET;
        ESP_LOGE(TAG, "chop mode switch to %s failed — not trusted",
                 spread_cycle ? "SpreadCycle" : "StealthChop");
    }

    tmc_uart_unlock();
    return result;
}
