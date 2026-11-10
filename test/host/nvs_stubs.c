/* nvs_stubs.c — no-op NVS backend for host tests of motors_limits.c. */

#include "nvs.h"

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle) {
    (void)namespace_name;
    (void)open_mode;
    *out_handle = 0;
    return ESP_ERR_NVS_NOT_FOUND;   /* no persisted values in host tests */
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value) {
    (void)handle;
    (void)key;
    (void)out_value;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value) {
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    (void)handle;
}
