#pragma once
#include "freertos/FreeRTOS.h"

typedef struct host_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait_ticks);
void vSemaphoreDelete(SemaphoreHandle_t sem);
