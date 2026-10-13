#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mount.h"
#include "motors.h"

/* Maximum JSON body length of a single frame (plus the 4-byte length prefix). */
#define PROTO_MAX_FRAME      2048
#define PROTO_MAX_TYPE_LEN   16

/* ── Session / identity (proto_session.c) ─────────────────────── */

void     proto_session_init(void);
uint32_t proto_boot_id(void);
uint32_t proto_config_rev(void);
void     proto_config_rev_bump(void);

/* Idempotency: record an ACTION outcome so a retried id replays the same
 * result instead of re-executing.  Cleared when the client reconnects. */
typedef struct {
    uint32_t id;
    bool ok;
    const char *error;  /* NULL when ok */
} proto_seen_entry_t;

bool proto_action_lookup(uint32_t id, proto_seen_entry_t *out);
void proto_action_record(uint32_t id, bool ok, const char *error);
void proto_session_reset(void);

/* ── JSON helpers (proto_json.c) ──────────────────────────────── */

/* True if `json` is a single well-formed JSON object with no duplicate
 * top-level keys and no trailing bytes. */
bool proto_json_validate(const char *json);
bool proto_json_has_key(const char *json, const char *key);
bool proto_json_get_string(const char *json, const char *key, char *out, size_t cap);
bool proto_json_get_float(const char *json, const char *key, float *out);
bool proto_json_get_int(const char *json, const char *key, int *out);
bool proto_json_get_u32(const char *json, const char *key, uint32_t *out);

/* Append a formatted chunk to a growing buffer; false on truncation. */
bool proto_buf_append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* ── Framing (proto_frame.c) ──────────────────────────────────── */

/* Feed received bytes into the frame parser. */
void proto_feed(const uint8_t *data, size_t len);
/* Drop any half-received frame (host reconnect). */
void proto_frame_reset(void);
/* Frame and send a null-terminated JSON body. */
void proto_send_body(const char *body);

/* ── Dispatch (proto_dispatch.c) ──────────────────────────────── */

void proto_dispatch(const char *body);

/* ── Handlers (one per family) — each writes a full response body ── */

void proto_handle_capabilities(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_state(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_config(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_control(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_action(const char *req, uint32_t id, char *out, size_t cap);

/* Builds the STATE response body (the coherent mount snapshot). */
void proto_state_snapshot(char *out, size_t cap, uint32_t id);

/* Small MountResult helper for rejections without a mount call. */
static inline MountResult proto_error(const char *message) {
    return (MountResult){ .ok = false, .message = message };
}
