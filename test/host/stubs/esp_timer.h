#pragma once
#include <stdint.h>
/* Monotonic microsecond clock.  In the host harness this is the virtual clock
 * advanced by ulTaskNotifyTake / vTaskDelay. */
int64_t esp_timer_get_time(void);
