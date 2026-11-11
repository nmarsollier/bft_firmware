#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

#define GPIO_NUM_1   1
#define GPIO_NUM_7   7
#define GPIO_NUM_9   9
#define GPIO_NUM_10  10
#define GPIO_NUM_12  12
#define GPIO_NUM_13  13
#define GPIO_NUM_15  15
#define GPIO_NUM_17  17
#define GPIO_NUM_18  18
#define GPIO_NUM_48  48

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
