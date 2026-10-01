// ht5023_dma.h — minimal HT5023 DMA helper (polling/blocking API)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_DMA_CH0 = 0,
  HT5023_DMA_CH1 = 1,
  HT5023_DMA_CH2 = 2,
} ht5023_dma_channel_t;

typedef enum {
  HT5023_DMA_REQ_SOFT = 0,
  HT5023_DMA_REQ_SPI_TX = 17,
  HT5023_DMA_REQ_SPI_RX = 18,
} ht5023_dma_request_t;

typedef enum {
  HT5023_DMA_WIDTH_8  = 0,
  HT5023_DMA_WIDTH_16 = 1,
  HT5023_DMA_WIDTH_32 = 2,
} ht5023_dma_width_t;

typedef enum {
  HT5023_DMA_ADDR_FIXED = 0,
  HT5023_DMA_ADDR_INC   = 1,
} ht5023_dma_addr_inc_t;

typedef struct {
  ht5023_dma_channel_t channel;
  ht5023_dma_request_t request;
  uintptr_t src_addr;
  uintptr_t dst_addr;
  uint16_t count;              // number of transfers (bytes/halfwords/words per width)
  ht5023_dma_width_t width;
  ht5023_dma_addr_inc_t src_inc;
  ht5023_dma_addr_inc_t dst_inc;
  bool block_mode;             // false = one transfer per request; true = block mode
  bool cyclic;                 // false = auto-close on completion
  uint8_t block_len;           // only used in block mode
} ht5023_dma_transfer_t;

// Adjust the encoded DMA request value by a small signed bias.
// This is intended for silicon/doc mismatches where request numbering is off by one.
void ht5023_dma_set_request_bias(int8_t bias);

// Configure the 16-bit DMA SRAM base used when encoding ARM SRAM addresses.
// `base` is ORed with the 15-bit SRAM byte offset for addresses in 0x20000000..0x20007FFF.
void ht5023_dma_set_sram_base(uint16_t base);

// Configure and start a DMA channel. Returns false on invalid args.
bool ht5023_dma_start(const ht5023_dma_transfer_t* xfer);

// Polls for channel completion. Returns false on timeout. On timeout the channel is disabled.
bool ht5023_dma_wait(ht5023_dma_channel_t channel, uint32_t timeout_polls);

// Convenience helper: start + wait.
bool ht5023_dma_run_blocking(const ht5023_dma_transfer_t* xfer, uint32_t timeout_polls);

// Returns the current channel transfer progress counter (CHNTCCNT).
uint16_t ht5023_dma_get_transferred_count(ht5023_dma_channel_t channel);

#ifdef __cplusplus
}
#endif
