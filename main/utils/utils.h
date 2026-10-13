#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* String utilities. */
bool string_join(char *buffer, size_t buffer_size, const char *const *items, size_t count, const char *separator,
                 const char *prefix, const char *suffix);

/* Number utilities. */
uint32_t float_to_uint32(float value);

float uint32_to_float(uint32_t value);
