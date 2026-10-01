// ht5023_dma.c — minimal HT5023 DMA helper (datasheet-derived register programming)
#include "ht5023_dma.h"
#include "ht5023_log.h"

#define HT5023_DMA_BASE          0x40016000u
#define HT5023_DMA_CH0_BASE      0x4001600Cu
#define HT5023_DMA_CH_STRIDE     0x18u

#define DMA_DMAIE_OFFSET         0x00u
#define DMA_DMAIF_OFFSET         0x04u
#define DMA_CHNSTA_OFFSET        0x08u

#define DMA_CHNCTL_OFFSET        0x00u
#define DMA_CHNSRC_OFFSET        0x04u
#define DMA_CHNTAR_OFFSET        0x08u
#define DMA_CHNCNT_OFFSET        0x0Cu
#define DMA_CHNTCCNT_OFFSET      0x10u
#define DMA_CHNBULKN_OFFSET      0x14u

// CHNCTL bit layout (HT502X user manual, DMA chapter)
#define DMA_CHNCTL_REQSEL_SHIFT     9u   // Channel[4:0] request selector field
#define DMA_CHNCTL_REQSEL_MASK      (0x1Fu << DMA_CHNCTL_REQSEL_SHIFT)
#define DMA_CHNCTL_DST_INC_SHIFT    7u   // DESTIN_INC[1:0] => bits 8:7
#define DMA_CHNCTL_SRC_INC_SHIFT    5u   // SOURC_INC[1:0] => bits 6:5
#define DMA_CHNCTL_CYCLE_BIT        (1u << 4)
#define DMA_CHNCTL_TMOD_BIT         (1u << 3)
#define DMA_CHNCTL_PSIZE_SHIFT      1u   // SIZE[1:0] => bits 2:1
#define DMA_CHNCTL_ENABLE_BIT       (1u << 0)

static int8_t g_dma_request_bias = 0;
static uint16_t g_dma_sram_base = 0x8000u;
static uint8_t g_dma_start_log_budget = 12u;

static inline volatile uint32_t* reg32(uint32_t base, uint32_t off) {
  return (volatile uint32_t*)(base + off);
}

static uint32_t dma_channel_base(ht5023_dma_channel_t ch) {
  if ((uint8_t)ch > 2u) return 0u;
  return HT5023_DMA_CH0_BASE + ((uint32_t)(uint8_t)ch * HT5023_DMA_CH_STRIDE);
}

static uint16_t dma_busy_mask(ht5023_dma_channel_t ch) {
  return (uint16_t)(1u << (uint8_t)ch);
}

static uint16_t dma_tcif_mask(ht5023_dma_channel_t ch) {
  return (uint16_t)(1u << (uint8_t)ch);
}

static uint16_t dma_bcif_mask(ht5023_dma_channel_t ch) {
  return (uint16_t)(1u << ((uint8_t)ch + 3u));
}

static uint16_t dma_teif_mask(ht5023_dma_channel_t ch) {
  return (uint16_t)(1u << ((uint8_t)ch + 8u));
}

static void dma_clear_flags(ht5023_dma_channel_t ch) {
  volatile uint32_t* dmaif = reg32(HT5023_DMA_BASE, DMA_DMAIF_OFFSET);
  const uint32_t keep_mask = (uint32_t)~((uint32_t)dma_tcif_mask(ch) |
                                         (uint32_t)dma_bcif_mask(ch) |
                                         (uint32_t)dma_teif_mask(ch));
  *dmaif &= keep_mask;
}

static uint16_t encode_inc_mode(ht5023_dma_addr_inc_t mode) {
  // Driver exposes only fixed/increment modes; datasheet also documents two block-internal cycle modes.
  return (uint16_t)((mode == HT5023_DMA_ADDR_INC) ? 0x1u : 0x0u);
}

static uint32_t encode_addr(uintptr_t addr) {
  (void)g_dma_sram_base;
  // Vendor CMSIS/device headers model CHNSRC/CHNTAR as full 32-bit addresses on HT502x.
  return (uint32_t)addr;
}

void ht5023_dma_set_request_bias(int8_t bias) {
  g_dma_request_bias = bias;
}

void ht5023_dma_set_sram_base(uint16_t base) {
  g_dma_sram_base = (uint16_t)(base & 0x8000u);
}

bool ht5023_dma_start(const ht5023_dma_transfer_t* xfer) {
  uint32_t ch_base;
  uint32_t ctl;
  uint32_t src_reg;
  uint32_t dst_reg;
  uint8_t request_sel;
  if (!xfer) return false;
  if (xfer->count == 0u) return false;
  if ((uint8_t)xfer->channel > 2u) return false;
  if ((uint8_t)xfer->width > 2u) return false;

  if (g_dma_request_bias < 0) {
    const uint8_t neg_bias = (uint8_t)(-g_dma_request_bias);
    if ((uint8_t)xfer->request < neg_bias) return false;
    request_sel = (uint8_t)xfer->request - neg_bias;
  } else {
    request_sel = (uint8_t)xfer->request + (uint8_t)g_dma_request_bias;
  }
  if (request_sel > 31u) return false;

  ch_base = dma_channel_base(xfer->channel);
  if (ch_base == 0u) return false;

  // Disable before reprogramming.
  *reg32(ch_base, DMA_CHNCTL_OFFSET) = 0u;
  dma_clear_flags(xfer->channel);

  src_reg = encode_addr(xfer->src_addr);
  dst_reg = encode_addr(xfer->dst_addr);
  *reg32(ch_base, DMA_CHNSRC_OFFSET) = src_reg;
  *reg32(ch_base, DMA_CHNTAR_OFFSET) = dst_reg;
  *reg32(ch_base, DMA_CHNCNT_OFFSET) = (uint32_t)xfer->count;
  *reg32(ch_base, DMA_CHNBULKN_OFFSET) = xfer->block_mode ? (uint32_t)xfer->block_len : 0u;

  ctl = 0u;
  ctl |= (uint32_t)(((uint32_t)request_sel & 0x1Fu) << DMA_CHNCTL_REQSEL_SHIFT);
  ctl |= (uint32_t)(encode_inc_mode(xfer->dst_inc) << DMA_CHNCTL_DST_INC_SHIFT);
  ctl |= (uint32_t)(encode_inc_mode(xfer->src_inc) << DMA_CHNCTL_SRC_INC_SHIFT);
  if (xfer->cyclic) ctl |= DMA_CHNCTL_CYCLE_BIT;
  if (xfer->block_mode) ctl |= DMA_CHNCTL_TMOD_BIT;
  ctl |= (uint32_t)(((uint32_t)xfer->width & 0x3u) << DMA_CHNCTL_PSIZE_SHIFT);

  // Program then enable.
  *reg32(ch_base, DMA_CHNCTL_OFFSET) = ctl;
  *reg32(ch_base, DMA_CHNCTL_OFFSET) = (ctl | DMA_CHNCTL_ENABLE_BIT);
  if (g_dma_start_log_budget != 0u) {
    g_dma_start_log_budget--;
    ht5023_log_write(HT5023_LOG_LEVEL_DEBUG,
                     "dma",
                     "start ch=%u req=%u reqsel=%u src=%p src16=0x%x dst=%p dst16=0x%x cnt=%u mode=%u bulk=%u ctl=0x%x bias=%d sbase=0x%x",
                     (unsigned)xfer->channel,
                     (unsigned)xfer->request,
                     (unsigned)request_sel,
                     xfer->src_addr,
                     (unsigned)src_reg,
                     xfer->dst_addr,
                     (unsigned)dst_reg,
                     (unsigned)xfer->count,
                     (unsigned)xfer->block_mode,
                     (unsigned)xfer->block_len,
                     (unsigned)(ctl | DMA_CHNCTL_ENABLE_BIT),
                     (int)g_dma_request_bias,
                     (unsigned)g_dma_sram_base);
  }
  return true;
}

bool ht5023_dma_wait(ht5023_dma_channel_t channel, uint32_t timeout_polls) {
  const uint32_t ch_base = dma_channel_base(channel);
  const uint16_t busy_mask = dma_busy_mask(channel);
  const uint16_t tcif_mask = dma_tcif_mask(channel);
  const uint16_t teif_mask = dma_teif_mask(channel);
  uint32_t initial_done;
  uint32_t target_count;
  bool saw_activity = false;
  volatile uint32_t* dmaif;
  volatile uint32_t* chnsta;
  volatile uint32_t* chntccnt;
  if (ch_base == 0u) return false;

  target_count = *reg32(ch_base, DMA_CHNCNT_OFFSET);
  dmaif = reg32(HT5023_DMA_BASE, DMA_DMAIF_OFFSET);
  chnsta = reg32(HT5023_DMA_BASE, DMA_CHNSTA_OFFSET);
  chntccnt = reg32(ch_base, DMA_CHNTCCNT_OFFSET);
  initial_done = *chntccnt;

  for (uint32_t i = 0u; i < timeout_polls; ++i) {
    const uint32_t flags = *dmaif;
    const uint32_t busy = *chnsta;
    const uint32_t done = *chntccnt;

    if ((flags & teif_mask) != 0u) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "dma",
                       "teif ch=%u flags=0x%x busy=0x%x done=%u target=%u ctl=0x%x src=0x%x dst=0x%x bulk=0x%x",
                       (unsigned)channel,
                       (unsigned)flags,
                       (unsigned)busy,
                       (unsigned)done,
                       (unsigned)target_count,
                       (unsigned)(*reg32(ch_base, DMA_CHNCTL_OFFSET)),
                       (unsigned)(*reg32(ch_base, DMA_CHNSRC_OFFSET)),
                       (unsigned)(*reg32(ch_base, DMA_CHNTAR_OFFSET)),
                       (unsigned)(*reg32(ch_base, DMA_CHNBULKN_OFFSET)));
      dma_clear_flags(channel);
      *reg32(ch_base, DMA_CHNCTL_OFFSET) &= (uint32_t)~DMA_CHNCTL_ENABLE_BIT;
      return false;
    }

    if ((flags & tcif_mask) != 0u) {
      dma_clear_flags(channel);
      return true;
    }

    if (((busy & busy_mask) != 0u) || (done != initial_done)) {
      saw_activity = true;
    }

    // Only accept completion after the channel has shown real activity.
    if (saw_activity && ((busy & busy_mask) == 0u) && (done == target_count)) {
      dma_clear_flags(channel);
      return true;
    }
  }

  // Abort channel on timeout.
  ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                   "dma",
                   "timeout ch=%u flags=0x%x busy=0x%x done=%u target=%u ctl=0x%x src=0x%x dst=0x%x bulk=0x%x",
                   (unsigned)channel,
                   (unsigned)(*dmaif),
                   (unsigned)(*chnsta),
                   (unsigned)(*chntccnt),
                   (unsigned)target_count,
                   (unsigned)(*reg32(ch_base, DMA_CHNCTL_OFFSET)),
                   (unsigned)(*reg32(ch_base, DMA_CHNSRC_OFFSET)),
                   (unsigned)(*reg32(ch_base, DMA_CHNTAR_OFFSET)),
                   (unsigned)(*reg32(ch_base, DMA_CHNBULKN_OFFSET)));
  *reg32(ch_base, DMA_CHNCTL_OFFSET) &= (uint32_t)~DMA_CHNCTL_ENABLE_BIT;
  dma_clear_flags(channel);
  return false;
}

bool ht5023_dma_run_blocking(const ht5023_dma_transfer_t* xfer, uint32_t timeout_polls) {
  if (!ht5023_dma_start(xfer)) return false;
  return ht5023_dma_wait(xfer->channel, timeout_polls);
}

uint16_t ht5023_dma_get_transferred_count(ht5023_dma_channel_t channel) {
  const uint32_t ch_base = dma_channel_base(channel);
  if (ch_base == 0u) return 0u;
  return (uint16_t)(*reg32(ch_base, DMA_CHNTCCNT_OFFSET) & 0xFFFFu);
}
