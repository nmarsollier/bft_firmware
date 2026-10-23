#pragma once
/* Keep arguments type-checked without depending on ESP-IDF logging. */
#include <stdio.h>
#define ESP_LOGE(tag, ...) do { (void)(tag); if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
