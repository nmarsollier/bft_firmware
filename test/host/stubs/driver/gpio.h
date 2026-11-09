#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14

#define GPIO_MODE_OUTPUT       (1)
#define GPIO_PULLUP_DISABLE    (0)
#define GPIO_PULLDOWN_DISABLE  (0)
#define GPIO_INTR_DISABLE      (0)

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level);
