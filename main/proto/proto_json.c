/* Proto — proto_json.c — structural JSON parsing and response building.
 *
 * Requests are small, flat JSON objects.  Locating fields by text search
 * (strstr/strchr) cannot tell a top-level key from one nested inside a value,
 * nor spot a duplicated key, and strtoul/strtol silently accept a sign or
 * overflow.  Instead the whole body is walked once with a small
 * recursive-descent parser that validates the document and resolves the
 * requested key structurally, so an invalid message — nested or duplicate
 * keys, bad numbers, unbalanced braces, trailing bytes — never reaches a
 * handler.
 */
#include "proto_internal.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enough for every distinct top-level key the protocol uses. */
#define JSON_MAX_KEYS 32

typedef enum {
    JSON_STRING = 's',
    JSON_NUMBER = 'n',
    JSON_OBJECT = 'o',
    JSON_ARRAY  = 'a',
    JSON_TRUE   = 't',
    JSON_FALSE  = 'f',
    JSON_NULL   = 'z',
} JsonType;

typedef struct {
    bool found;
    bool duplicate;
    JsonType type;
    const char *value;      /* first byte of the value ('"' for strings) */
    const char *value_end;  /* one past the last byte of the value */
} JsonField;

/* ── low-level scanners ─────────────────────────────────────────── */

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') {
        (*p)++;
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a JSON string starting at the '"' under *p.  Validates escapes and
 * advances *p one past the closing quote. */
static bool parse_string(const char **p) {
    const char *s = *p;
    if (*s != '"') return false;
    s++;
    while (*s != '\0' && *s != '"') {
        if ((unsigned char) *s < 0x20) return false;   /* raw control char */
        if (*s == '\\') {
            s++;
            switch (*s) {
            case '"': case '\\': case '/':
            case 'b': case 'f': case 'n': case 'r': case 't':
                s++;
                break;
            case 'u':
                s++;
                for (int i = 0; i < 4; i++) {
                    if (hex_value(*s) < 0) return false;
                    s++;
                }
                break;
            default:
                return false;
            }
        } else {
            s++;
        }
    }
    if (*s != '"') return false;
    *p = s + 1;
    return true;
}

/* Parse a JSON number starting at *p.  Validates grammar, advances past it. */
static bool parse_number(const char **p) {
    const char *s = *p;
    if (*s == '-') s++;
    if (*s == '0') {
        s++;
        if (*s >= '0' && *s <= '9') return false;   /* leading zero */
    } else if (*s >= '1' && *s <= '9') {
        while (*s >= '0' && *s <= '9') s++;
    } else {
        return false;
    }
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return false;
        while (*s >= '0' && *s <= '9') s++;
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') s++;
        if (*s < '0' || *s > '9') return false;
        while (*s >= '0' && *s <= '9') s++;
    }
    *p = s;
    return true;
}

static bool parse_literal(const char **p, const char *lit) {
    size_t n = strlen(lit);
    if (strncmp(*p, lit, n) != 0) return false;
    *p += n;
    return true;
}

/* Parse any JSON value starting at *p, validating structure and advancing *p
 * one past it.  Objects and arrays recurse; nested keys are skipped here and
 * never mistaken for top-level members. */
static bool parse_value(const char **p) {
    skip_ws(p);
    const char *s = *p;
    switch (*s) {
    case '"':
        return parse_string(p);
    case '{':
        (*p)++;
        skip_ws(p);
        if (**p == '}') { (*p)++; return true; }
        for (;;) {
            skip_ws(p);
            if (!parse_string(p)) return false;
            skip_ws(p);
            if (**p != ':') return false;
            (*p)++;
            if (!parse_value(p)) return false;
            skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; return true; }
            return false;
        }
    case '[':
        (*p)++;
        skip_ws(p);
        if (**p == ']') { (*p)++; return true; }
        for (;;) {
            if (!parse_value(p)) return false;
            skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; return true; }
            return false;
        }
    case 't':
        return parse_literal(p, "true");
    case 'f':
        return parse_literal(p, "false");
    case 'n':
        return parse_literal(p, "null");
    default:
        if (*s == '-' || (*s >= '0' && *s <= '9')) return parse_number(p);
        return false;
    }
}

static JsonType value_type(const char *v) {
    switch (*v) {
    case '"': return JSON_STRING;
    case '{': return JSON_OBJECT;
    case '[': return JSON_ARRAY;
    case 't': return JSON_TRUE;
    case 'f': return JSON_FALSE;
    case 'n': return JSON_NULL;
    default:  return JSON_NUMBER;
    }
}

/* ── key comparison (protocol keys are plain ASCII, no escapes) ──── */

/* Compare a raw JSON key (pointer at its opening '"') against a plain C
 * string.  Any backslash means "not this plain key" — the field is then
 * simply treated as absent, never mis-matched. */
static bool raw_key_equals(const char *raw, const char *want) {
    if (*raw != '"') return false;
    const char *s = raw + 1;
    while (*s != '"' && *s != '\0') {
        if (*s == '\\' || *want == '\0') return false;
        if (*s != *want) return false;
        s++;
        want++;
    }
    return *s == '"' && *want == '\0';
}

static bool raw_keys_identical(const char *a, const char *b) {
    if (*a != '"' || *b != '"') return false;
    const char *pa = a + 1;
    const char *pb = b + 1;
    while (*pa != '"' && *pb != '"' && *pa != '\0' && *pb != '\0') {
        if (*pa == '\\' || *pb == '\\') return false;
        if (*pa != *pb) return false;
        pa++;
        pb++;
    }
    return *pa == '"' && *pb == '"';
}

/* ── document scan ──────────────────────────────────────────────── */

typedef struct {
    const char *want;                 /* key to capture, or NULL (validate only) */
    JsonField field;                  /* capture result for `want` */
    const char *seen[JSON_MAX_KEYS];  /* raw opening quotes of seen top-level keys */
    size_t seen_count;
    bool duplicate;                   /* some top-level key repeated */
} JsonScan;

/* Scan one top-level JSON object.  On entry *p points just past '{'; on
 * success *p is advanced one past the matching '}'.  Returns false on a
 * malformed object. */
static bool scan_object(const char **p, JsonScan *sc) {
    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return true;
    }
    for (;;) {
        skip_ws(p);
        const char *key = *p;   /* opening '"' of the key */
        if (!parse_string(p)) return false;

        for (size_t i = 0; i < sc->seen_count; i++) {
            if (raw_keys_identical(sc->seen[i], key)) {
                sc->duplicate = true;
                break;
            }
        }
        if (sc->seen_count < JSON_MAX_KEYS) {
            sc->seen[sc->seen_count++] = key;
        }

        skip_ws(p);
        if (**p != ':') return false;
        (*p)++;
        skip_ws(p);

        const char *val = *p;
        if (!parse_value(p)) return false;
        const char *val_end = *p;

        if (sc->want != NULL && raw_key_equals(key, sc->want)) {
            if (sc->field.found) {
                sc->field.duplicate = true;
            } else {
                sc->field.found = true;
                sc->field.type = value_type(val);
                sc->field.value = val;
                sc->field.value_end = val_end;
            }
        }

        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; return true; }
        return false;
    }
}

/* Validate the whole body as one JSON object and, when `key` is non-NULL,
 * resolve it structurally.  Returns false on a malformed document (including
 * trailing bytes). */
static bool find_top_level(const char *json, const char *key, JsonField *out) {
    JsonScan sc = {0};
    sc.want = key;
    const char *p = json;
    if (p == NULL) return false;
    skip_ws(&p);
    if (*p != '{') return false;
    p++;
    if (!scan_object(&p, &sc)) return false;
    skip_ws(&p);
    if (*p != '\0') return false;
    *out = sc.field;
    return true;
}

/* ── public API ─────────────────────────────────────────────────── */

bool proto_json_validate(const char *json) {
    JsonScan sc = {0};
    sc.want = NULL;
    const char *p = json;
    if (p == NULL) return false;
    skip_ws(&p);
    if (*p != '{') return false;
    p++;
    if (!scan_object(&p, &sc)) return false;
    skip_ws(&p);
    if (*p != '\0') return false;
    return !sc.duplicate;
}

bool proto_json_has_key(const char *json, const char *key) {
    JsonField f;
    if (!find_top_level(json, key, &f)) return false;
    return f.found && !f.duplicate;
}

static size_t encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    } else {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
}

/* Decode a string value's escapes into `out`.  `v` points at the opening '"',
 * `end` one past the closing '"' (both already validated by parse_string). */
static bool decode_string(const char *v, const char *end, char *out, size_t cap) {
    const char *s = v + 1;
    size_t i = 0;
    while (s < end - 1) {   /* stop before the closing quote */
        unsigned char c = (unsigned char) *s;
        if (c == '\\') {
            s++;
            switch (*s) {
            case '"':  if (i < cap - 1) out[i++] = '"';  s++; break;
            case '\\': if (i < cap - 1) out[i++] = '\\'; s++; break;
            case '/':  if (i < cap - 1) out[i++] = '/';  s++; break;
            case 'b':  if (i < cap - 1) out[i++] = '\b'; s++; break;
            case 'f':  if (i < cap - 1) out[i++] = '\f'; s++; break;
            case 'n':  if (i < cap - 1) out[i++] = '\n'; s++; break;
            case 'r':  if (i < cap - 1) out[i++] = '\r'; s++; break;
            case 't':  if (i < cap - 1) out[i++] = '\t'; s++; break;
            case 'u': {
                uint32_t cp = 0;
                for (int k = 1; k <= 4; k++) {
                    cp = (cp << 4) | (uint32_t) hex_value(s[k]);
                }
                s += 5;
                char enc[3];
                size_t n = encode_utf8(cp, enc);
                for (size_t k = 0; k < n; k++) {
                    if (i < cap - 1) out[i++] = enc[k];
                }
                break;
            }
            default:
                return false;   /* unreachable: parse_string validated the escape */
            }
        } else {
            if (i < cap - 1) out[i++] = (char) c;
            s++;
        }
    }
    out[i] = '\0';
    return true;
}

bool proto_json_get_string(const char *json, const char *key, char *out, size_t cap) {
    if (out == NULL || cap == 0) return false;
    JsonField f;
    if (!find_top_level(json, key, &f)) return false;
    if (!f.found || f.duplicate || f.type != JSON_STRING) return false;
    return decode_string(f.value, f.value_end, out, cap);
}

bool proto_json_get_float(const char *json, const char *key, float *out) {
    if (out == NULL) return false;
    JsonField f;
    if (!find_top_level(json, key, &f)) return false;
    if (!f.found || f.duplicate || f.type != JSON_NUMBER) return false;

    size_t n = (size_t) (f.value_end - f.value);
    if (n >= 64) return false;
    char buf[64];
    memcpy(buf, f.value, n);
    buf[n] = '\0';

    char *end = NULL;
    float val = strtof(buf, &end);
    if (end == buf || *end != '\0' || !isfinite(val)) return false;
    *out = val;
    return true;
}

static bool parse_int_span(const char *s, const char *e, int *out) {
    if (s >= e) return false;
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
        if (s >= e) return false;
    }
    int64_t v = 0;
    int64_t limit = neg ? ((int64_t) INT_MAX + 1) : (int64_t) INT_MAX;
    for (const char *p = s; p < e; p++) {
        if (*p < '0' || *p > '9') return false;   /* rejects '.', 'e', '+' */
        v = v * 10 + (int64_t) (*p - '0');
        if (v > limit) return false;
    }
    *out = (int) (neg ? -v : v);
    return true;
}

bool proto_json_get_int(const char *json, const char *key, int *out) {
    if (out == NULL) return false;
    JsonField f;
    if (!find_top_level(json, key, &f)) return false;
    if (!f.found || f.duplicate || f.type != JSON_NUMBER) return false;
    return parse_int_span(f.value, f.value_end, out);
}

static bool parse_u32_span(const char *s, const char *e, uint32_t *out) {
    if (s >= e) return false;
    uint64_t v = 0;
    for (const char *p = s; p < e; p++) {
        if (*p < '0' || *p > '9') return false;   /* rejects '-', '.', 'e', '+' */
        v = v * 10 + (uint64_t) (*p - '0');
        if (v > UINT32_MAX) return false;
    }
    *out = (uint32_t) v;
    return true;
}

bool proto_json_get_u32(const char *json, const char *key, uint32_t *out) {
    if (out == NULL) return false;
    JsonField f;
    if (!find_top_level(json, key, &f)) return false;
    if (!f.found || f.duplicate || f.type != JSON_NUMBER) return false;
    return parse_u32_span(f.value, f.value_end, out);
}

bool proto_buf_append(char *buf, size_t cap, size_t *len, const char *fmt, ...) {
    if (buf == NULL || len == NULL || *len >= cap) {
        return false;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t) n >= cap - *len) {
        return false;   /* truncated */
    }
    *len += (size_t) n;
    return true;
}
