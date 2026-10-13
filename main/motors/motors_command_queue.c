/* Motors - motors_command_queue.c
 *
 * Purpose: FreeRTOS command queue for the motion task.
 * Owns the queue handle, lifecycle, send, and clear helpers.
 */

#include "motors.h"
#include "motors_internal.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "MOTORS_MOTION_CMD";

/* Queue handle — shared across the motors module via motors_internal.h. */
QueueHandle_t motion_cmd_queue = NULL;

/* --------------------------------------------------------------------------
 * Queue lifecycle.
 * -------------------------------------------------------------------------- */
esp_err_t motors_queue_init(void) {
    motion_cmd_queue = xQueueCreate(10, sizeof(MotionCommand));
    if (motion_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create motion command queue");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Shared helper — send a command to the motion task queue.
 *
 * Every command is sent to the back (FIFO order).  STOP / PARK / DISABLE
 * callers reset the queue before sending so they always run immediately.
 * -------------------------------------------------------------------------- */
bool motors_queue_put(MotionCommand *cmd) {
    if (motion_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Motion command queue not initialized");
        return false;
    }

    /* Stamp the current stop epoch so a STOP that lands after this enqueue
     * invalidates the command at execution time. */
    cmd->generation = motors_stop_generation;

    if (xQueueSendToBack(motion_cmd_queue, cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, dropping cmd type=%d", cmd->type);
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Public — atomically empty the command queue.
 * -------------------------------------------------------------------------- */
void motors_queue_clear(void) {
    if (motion_cmd_queue != NULL) {
        xQueueReset(motion_cmd_queue);
    }
}
