#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Mirrors the ESP-IDF RMT symbol layout the production encoder writes. */
typedef struct {
    uint16_t duration0 : 15;
    uint16_t level0 : 1;
    uint16_t duration1 : 15;
    uint16_t level1 : 1;
} rmt_symbol_word_t;

typedef struct host_rmt_channel *rmt_channel_handle_t;
typedef struct host_rmt_encoder *rmt_encoder_handle_t;

typedef struct {
    int gpio_num;
    int clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
    int intr_priority;
    struct { uint32_t with_dma : 1; } flags;
} rmt_tx_channel_config_t;

typedef struct { int _unused; } rmt_copy_encoder_config_t;

typedef struct {
    int num_symbols;
} rmt_tx_done_event_data_t;

typedef bool (*rmt_tx_done_callback_t)(rmt_channel_handle_t,
                                       const rmt_tx_done_event_data_t *, void *);

typedef struct {
    rmt_tx_done_callback_t on_trans_done;
} rmt_tx_event_callbacks_t;

typedef struct { uint32_t loop_count; } rmt_transmit_config_t;

#define RMT_CLK_SRC_DEFAULT 0

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *cfg, rmt_channel_handle_t *out);
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *cfg, rmt_encoder_handle_t *out);
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t ch,
                                          const rmt_tx_event_callbacks_t *cbs,
                                          void *user);
esp_err_t rmt_enable(rmt_channel_handle_t ch);
esp_err_t rmt_disable(rmt_channel_handle_t ch);
esp_err_t rmt_transmit(rmt_channel_handle_t ch, rmt_encoder_handle_t enc,
                       const void *payload, size_t payload_bytes,
                       const rmt_transmit_config_t *cfg);
esp_err_t rmt_del_channel(rmt_channel_handle_t ch);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t enc);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t ch, int timeout_ms);
