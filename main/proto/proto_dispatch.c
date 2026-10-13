/* Proto — proto_dispatch.c — route a request body to its family handler. */
#include "proto_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "PROTO_DISPATCH";

void proto_dispatch(const char *body) {
    char type[PROTO_MAX_TYPE_LEN] = {0};
    uint32_t id = 0;

    char out[PROTO_MAX_FRAME];

    /* Reject a structurally invalid request (bad JSON, duplicate top-level
     * keys, trailing bytes) before it can reach any handler. */
    if (!proto_json_validate(body)) {
        ESP_LOGW(TAG, "rejecting invalid request body");
        snprintf(out, sizeof(out),
                 "{\"type\":\"error\",\"id\":0,\"ok\":false,\"error\":\"invalid request\"}");
        proto_send_body(out);
        return;
    }

    proto_json_get_string(body, "type", type, sizeof(type));

    /* `id` is optional; when present it must be a valid unsigned integer
     * (rejects negatives, fractions and overflow). */
    if (proto_json_has_key(body, "id") && !proto_json_get_u32(body, "id", &id)) {
        ESP_LOGW(TAG, "rejecting invalid id");
        snprintf(out, sizeof(out),
                 "{\"type\":\"error\",\"id\":0,\"ok\":false,\"error\":\"invalid id\"}");
        proto_send_body(out);
        return;
    }

    if (strcmp(type, "capabilities") == 0) {
        proto_handle_capabilities(body, id, out, sizeof(out));
    } else if (strcmp(type, "state") == 0) {
        proto_handle_state(body, id, out, sizeof(out));
    } else if (strcmp(type, "config") == 0) {
        proto_handle_config(body, id, out, sizeof(out));
    } else if (strcmp(type, "control") == 0) {
        proto_handle_control(body, id, out, sizeof(out));
    } else if (strcmp(type, "action") == 0) {
        proto_handle_action(body, id, out, sizeof(out));
    } else {
        const char *name = type[0] != '\0' ? type : "unknown";
        snprintf(out, sizeof(out),
                 "{\"type\":\"%s\",\"id\":%lu,\"ok\":false,\"error\":\"unknown type\"}",
                 name, (unsigned long) id);
        ESP_LOGW(TAG, "unknown request type '%s'", name);
    }

    proto_send_body(out);
}
