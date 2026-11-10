#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Minimal NVS stand-in for host-side tests — the persisted values are simply
 * dropped (no-op), so motors_limits.c compiles and runs without flash. */

typedef int nvs_handle_t;

#define NVS_READONLY  1
#define NVS_READWRITE 2

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
