/* Runtime - runtime_loop.c
 *
 * Purpose: run the periodic mount cycle for inputs.
 */
#include "runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "led.h"
#include "motors.h"
#include "power.h"

#define MAIN_LOOP_PERIOD_MS     200
#define MAIN_TASK_STACK_SIZE    4096
#define MAIN_TASK_PRIORITY      5

/*
 * Business use case: orchestrate the mount's periodic operational cycle.
 *
 * Objective: keep input polling running at a stable cadence.
 * Motor motion runs in its own task owned by the motors_motion subsystem.
 */
static void main_loop_task(void *arg) {
    (void) arg;
    const TickType_t period = pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS);

    while (true) {
        motors_update_power(power_has_external());
        led_update();
        buzzer_update();
        vTaskDelay(period);
    }
}

/*
 * Business use case: start the main runtime in the background.
 *
 * Objective: delegate continuous operation to a dedicated FreeRTOS task.
 */
void setup_runtime_start(void) {
    xTaskCreatePinnedToCore(
        main_loop_task,
        "main_loop",
        MAIN_TASK_STACK_SIZE,
        NULL,
        MAIN_TASK_PRIORITY,
        NULL,
        0);  /* CPU 0 — keep CPU 1 isolated for the motion task */
}
