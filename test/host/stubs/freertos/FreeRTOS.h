#pragma once
/* Minimal FreeRTOS stand-in for host-side tests.  Only the primitives the
 * compiled production code touches are provided; everything is single-threaded
 * and the "scheduler" is driven explicitly by the test harness. */
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t portMUX_TYPE;
typedef void *TaskHandle_t;

#define portMUX_INITIALIZER_UNLOCKED 0u
#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux)  (void)(mux)

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY ((TickType_t)0xffffffffu)

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

typedef enum {
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite,
} eNotifyAction;

/* Task notify / delay primitives (declared here so any includer of FreeRTOS.h
 * gets them, matching the transitive visibility the real headers provide). */
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_count, TickType_t wait_ticks);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
                                   uint32_t stack, void *arg, uint32_t prio,
                                   TaskHandle_t *out, int core);
