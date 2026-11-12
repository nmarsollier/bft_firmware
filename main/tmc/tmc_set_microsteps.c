/* TMC — tmc_set_microsteps.c
 *
 * Purpose: change the microstep resolution on both drivers at runtime —
 * read-modify-write of CHOPCONF.MRES, verified by read-back with retries.
 */

#include "tmc.h"
#include "tmc_internal.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "TMC_SET_MICROSTEPS";

/*
 * Read-modify-write CHOPCONF.MRES and verify by read-back.  CHOPCONF is
 * read-write, so the current value is read, only the MRES field [27:24] is
 * replaced (preserving TOFF/HSTRT/HEND/TBL/INTPOL), and the write is confirmed
 * by reading MRES back.  Retries 3 times before giving up.
 */
static esp_err_t tmc_set_chopconf_mres(const TmcAxis *axis, uint8_t mres) {
    uint32_t chopconf = 0;
    if (tmc_read_register(axis, TMC_REG_CHOPCONF, &chopconf) != ESP_OK) {
        return ESP_FAIL;
    }
    uint32_t next = (chopconf & ~(0x0Fu << 24)) | ((uint32_t) mres << 24);

    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t readback = 0;
        if (tmc_write_register(axis, TMC_REG_CHOPCONF, next) == ESP_OK
            && tmc_read_register(axis, TMC_REG_CHOPCONF, &readback) == ESP_OK
            && (((readback >> 24) & 0x0F) == mres)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return ESP_FAIL;
}

esp_err_t tmc2209_set_microsteps(uint16_t microsteps)
{
    uint8_t mres = 0;
    if (!tmc_microsteps_to_mres(microsteps, &mres)) {
        ESP_LOGE(TAG, "invalid microstep value %u", (unsigned) microsteps);
        return ESP_ERR_INVALID_ARG;
    }

    if (tmc_uart_lock() != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < TMC_AXIS_COUNT; i++) {
        if (!tmc_uart_ok[i]) {
            result = ESP_FAIL;
            break;
        }
        if (tmc_set_chopconf_mres(&tmc_axes[i], mres) != ESP_OK) {
            result = ESP_FAIL;
            break;
        }
    }

    if (result == ESP_OK) {
        tmc_microsteps = microsteps;   /* verified on both axes */
        ESP_LOGI(TAG, "microsteps set to %u on both axes", (unsigned) microsteps);
    } else {
        tmc_status = TMC_ERROR;
        tmc_error = TMC_ERROR_MRES_SET;
        ESP_LOGE(TAG, "microstep switch to %u failed — not trusted", (unsigned) microsteps);
    }

    tmc_uart_unlock();
    return result;
}
