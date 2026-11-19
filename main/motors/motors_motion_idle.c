/* Motors - motors_motion_idle.c
 *
 * Purpose: the motion task's idle signalling — a binary semaphore the motion
 * task gives while idle and takes while running, so control operations
 * (home/limits setters) can wait for it to actually stop before touching
 * counters/limits.
 *
 * The semaphore is private here; both sides go through this small API so no
 * caller reaches into another module's internal state.
 */
#include "motors_internal.h"

#include "freertos/task.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_idle_sem = NULL;

/* Create the idle semaphore.  Called once by the motion task init. */
esp_err_t motors_motion_idle_init(void) {
    s_idle_sem = xSemaphoreCreateBinary();
    return (s_idle_sem != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

/* The motion task signals it is idle (about to block on the command queue). */
void motors_motion_idle_give(void) {
    if (s_idle_sem != NULL) {
        xSemaphoreGive(s_idle_sem);
    }
}

/* The motion task signals it is busy (it just dequeued a command). */
void motors_motion_idle_take(void) {
    if (s_idle_sem != NULL) {
        xSemaphoreTake(s_idle_sem, 0);
    }
}

/*
 * Wait (bounded) until the motion task is idle, so a caller can safely modify
 * counters/limits without racing the in-flight motion loop.  Callers must
 * invoke motors_stop() first, which sets active=false and bumps the stop epoch;
 * this then blocks until the motion task has actually left its loop.
 *
 * Returns true only when the idle semaphore was actually taken (the motion task
 * is confirmed idle).  Returns false on timeout — the motion task was still
 * running — so the caller must NOT modify counters/limits.
 */
bool motors_motion_wait_idle(void) {
    if (s_idle_sem == NULL) {
        return false;
    }
    /* The idle semaphore is a binary token: present = motion task idle.  Take
     * it to confirm idle, then give it straight back so a later wait_idle (and
     * the motion task's own give/take bookkeeping) still sees the idle state
     * instead of a stale, consumed token. */
    if (xSemaphoreTake(s_idle_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    xSemaphoreGive(s_idle_sem);
    return true;
}
