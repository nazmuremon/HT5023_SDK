// ht5023_st7789.c — ST7789 LCD controller driver (RGB565, callback-based bus)
#include "ht5023_st7789.h"
#include "ht5023_log.h"

#include <string.h>

#define ST7789_CMD_SWRESET  0x01u
#define ST7789_CMD_SLPOUT   0x11u
#define ST7789_CMD_NORON    0x13u
#define ST7789_CMD_INVOFF   0x20u
#define ST7789_CMD_INVON    0x21u
#define ST7789_CMD_DISPON   0x29u
#define ST7789_CMD_CASET    0x2Au
#define ST7789_CMD_RASET    0x2Bu
#define ST7789_CMD_RAMWR    0x2Cu
#define ST7789_CMD_MADCTL   0x36u
#define ST7789_CMD_COLMOD   0x3Au

#define ST7789_MADCTL_MY    0x80u
#define ST7789_MADCTL_MX    0x40u
#define ST7789_MADCTL_MV    0x20u
#define ST7789_MADCTL_BGR   0x08u
#define ST7789_STREAM_CHUNK_BYTES 512u

static uint8_t g_st7789_dma_log_budget = 16u;

static inline volatile uint16_t* reg16_local(uintptr_t addr) {
  return (volatile uint16_t*)addr;
}

static void delay_ms(ht5023_st7789_t* lcd, uint32_t ms) {
  if (lcd && lcd->cfg.delay_ms) lcd->cfg.delay_ms(ms, lcd->cfg.user);
}

static void cs_write(ht5023_st7789_t* lcd, bool high) {
  if (lcd && lcd->cfg.cs_write) lcd->cfg.cs_write(high, lcd->cfg.user);
}

static void dc_write(ht5023_st7789_t* lcd, bool high) {
  if (lcd && lcd->cfg.dc_write) lcd->cfg.dc_write(high, lcd->cfg.user);
}

static void dma_note_success(ht5023_st7789_t* lcd) {
  if (!lcd || !lcd->dma.configured) return;
  lcd->dma.success_count++;
  lcd->dma.fail_streak = 0u;
}

static void dma_note_fallback(ht5023_st7789_t* lcd) {
  if (!lcd || !lcd->dma.configured) return;
  lcd->dma.fallback_count++;
  if (lcd->cfg.write && lcd->dma.cfg.max_fail_streak != 0u) {
    if (lcd->dma.fail_streak < 255u) lcd->dma.fail_streak++;
    if (lcd->dma.fail_streak >= lcd->dma.cfg.max_fail_streak) lcd->dma.enabled = false;
  }
}

static void log_dma_io_regs(ht5023_st7789_t* lcd, const char* why) {
  uintptr_t dat_addr;
  uintptr_t sta_addr;
  uintptr_t ssn_addr;
  if (!lcd || !lcd->dma.configured || lcd->dma.cfg.tx_reg_addr < 4u) return;
  dat_addr = lcd->dma.cfg.tx_reg_addr;
  sta_addr = dat_addr - 4u;
  ssn_addr = dat_addr + 4u;
  if (g_st7789_dma_log_budget == 0u) return;
  g_st7789_dma_log_budget--;
  ht5023_log_write(HT5023_LOG_LEVEL_DEBUG,
                   "st7789",
                   "%s spi_sta=0x%x spi_dat=0x%x spi_ssn=0x%x tx_reg=0x%x",
                   why,
                   (unsigned)(*reg16_local(sta_addr)),
                   (unsigned)(*reg16_local(dat_addr)),
                   (unsigned)(*reg16_local(ssn_addr)),
                   (unsigned)lcd->dma.cfg.tx_reg_addr);
}

static void clear_spi_status(ht5023_st7789_t* lcd) {
  uintptr_t dat_addr;
  uintptr_t sta_addr;
  volatile uint16_t dummy;
  if (!lcd || !lcd->dma.configured || lcd->dma.cfg.tx_reg_addr < 4u) return;
  dat_addr = lcd->dma.cfg.tx_reg_addr;
  sta_addr = dat_addr - 4u;
  dummy = *reg16_local(sta_addr);
  dummy = *reg16_local(dat_addr);
  (void)dummy;
}

static size_t dma_stage_capacity_bytes(const ht5023_st7789_t* lcd) {
  if (!lcd || !lcd->dma.configured) return 0u;
  if (!lcd->dma.cfg.stage_buffer || (lcd->dma.cfg.stage_buffer_len < 2u)) return 0u;
  if ((((uintptr_t)lcd->dma.cfg.stage_buffer) & 0x1u) != 0u) return 0u;
  return lcd->dma.cfg.stage_buffer_len / sizeof(uint16_t);
}

static size_t dma_rx_stage_capacity_bytes(const ht5023_st7789_t* lcd) {
  if (!lcd || !lcd->dma.configured) return 0u;
  if (!lcd->dma.cfg.rx_stage_buffer || (lcd->dma.cfg.rx_stage_buffer_len < 2u)) return 0u;
  if ((((uintptr_t)lcd->dma.cfg.rx_stage_buffer) & 0x1u) != 0u) return 0u;
  return lcd->dma.cfg.rx_stage_buffer_len / sizeof(uint16_t);
}

static void pack_dma_stage_words(uint16_t* stage_words, const uint8_t* src, size_t count) {
  if (!stage_words || !src) return;
  for (size_t i = 0u; i < count; ++i) stage_words[i] = (uint16_t)src[i];
}

static bool dma_transfer_bytes(ht5023_st7789_t* lcd, const uint8_t* bytes, size_t len, bool force_dma) {
  const size_t stage_capacity = dma_stage_capacity_bytes(lcd);
  const size_t rx_stage_capacity = dma_rx_stage_capacity_bytes(lcd);
  if (!lcd || !bytes || len == 0u) return false;
  if (!lcd->dma.configured || !lcd->dma.enabled) return false;
  if (!force_dma && len < lcd->dma.cfg.min_bytes) return false;
  if (lcd->dma.cfg.tx_reg_addr == 0u) return false;
  if (stage_capacity == 0u) return false;
  if ((lcd->dma.cfg.rx_reg_addr != 0u) && (rx_stage_capacity == 0u)) return false;

  while (len != 0u) {
    size_t chunk_len = len;
    size_t dma_len;
    size_t completed = 0u;
    size_t prime_count = lcd->dma.cfg.prime_tx ? 1u : 0u;
    uint16_t* stage_words = (uint16_t*)lcd->dma.cfg.stage_buffer;
    uint16_t* rx_stage_words = (uint16_t*)lcd->dma.cfg.rx_stage_buffer;
    bool primed = false;
    bool rx_started = false;
    ht5023_dma_transfer_t xfer;
    ht5023_dma_transfer_t rx_xfer;

    if (chunk_len > stage_capacity + prime_count) chunk_len = stage_capacity + prime_count;
    if ((lcd->dma.cfg.rx_reg_addr != 0u) && (chunk_len > rx_stage_capacity)) chunk_len = rx_stage_capacity;
    if (chunk_len == 0u) return false;

    clear_spi_status(lcd);

    if (chunk_len <= prime_count) {
      for (size_t i = 0u; i < chunk_len; ++i) {
        if (!lcd->dma.cfg.prime_tx || !lcd->dma.cfg.prime_tx(bytes[i], lcd->cfg.user)) {
          ht5023_log_write(HT5023_LOG_LEVEL_ERROR, "st7789", "dma prime failed len=%lu", (unsigned long)len);
          dma_note_fallback(lcd);
          return false;
        }
        if (lcd->dma.cfg.wait_complete && !lcd->dma.cfg.wait_complete(lcd->cfg.user)) {
          ht5023_log_write(HT5023_LOG_LEVEL_ERROR, "st7789", "spi wait_complete failed primed-only");
          dma_note_fallback(lcd);
          return false;
        }
      }
      dma_note_success(lcd);
      bytes += chunk_len;
      len -= chunk_len;
      continue;
    }

    dma_len = chunk_len - prime_count;
    pack_dma_stage_words(stage_words, bytes + prime_count, dma_len);

    xfer.channel = lcd->dma.cfg.channel;
    xfer.request = lcd->dma.cfg.request;
    xfer.src_addr = (uintptr_t)stage_words;
    xfer.dst_addr = lcd->dma.cfg.tx_reg_addr;
    xfer.count = (uint16_t)dma_len;
    xfer.width = HT5023_DMA_WIDTH_16;
    xfer.src_inc = HT5023_DMA_ADDR_INC;
    xfer.dst_inc = HT5023_DMA_ADDR_FIXED;
    xfer.block_mode = false;
    xfer.cyclic = false;
    xfer.block_len = 0u;

    if (lcd->dma.cfg.rx_reg_addr != 0u) {
      rx_xfer.channel = lcd->dma.cfg.rx_channel;
      rx_xfer.request = lcd->dma.cfg.rx_request;
      rx_xfer.src_addr = lcd->dma.cfg.rx_reg_addr;
      rx_xfer.dst_addr = (uintptr_t)rx_stage_words;
      rx_xfer.count = (uint16_t)dma_len;
      rx_xfer.width = HT5023_DMA_WIDTH_16;
      rx_xfer.src_inc = HT5023_DMA_ADDR_FIXED;
      rx_xfer.dst_inc = HT5023_DMA_ADDR_INC;
      rx_xfer.block_mode = false;
      rx_xfer.cyclic = false;
      rx_xfer.block_len = 0u;
    }

    if (g_st7789_dma_log_budget != 0u) {
      g_st7789_dma_log_budget--;
      ht5023_log_write(HT5023_LOG_LEVEL_DEBUG,
                       "st7789",
                       "dma prep chunk=%lu dma_len=%lu prime=%lu src=%p rx=%p b0=0x%x b1=0x%x",
                       (unsigned long)chunk_len,
                       (unsigned long)dma_len,
                       (unsigned long)prime_count,
                       stage_words,
                       rx_stage_words,
                       (unsigned)bytes[0],
                       (unsigned)((chunk_len > 1u) ? bytes[1] : 0u));
    }

    if ((lcd->dma.cfg.rx_reg_addr != 0u) && !ht5023_dma_start(&rx_xfer)) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "st7789",
                       "dma rx start failed chunk=%lu rem=%lu",
                       (unsigned long)chunk_len,
                       (unsigned long)len);
      dma_note_fallback(lcd);
      return false;
    }
    rx_started = (lcd->dma.cfg.rx_reg_addr != 0u);

    if ((dma_len != 0u) && !ht5023_dma_start(&xfer)) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "st7789",
                       "dma start failed chunk=%lu rem=%lu",
                       (unsigned long)chunk_len,
                       (unsigned long)len);
      dma_note_fallback(lcd);
      return false;
    }

    // Arm DMA first, then kick the SPI engine with the first byte.
    if (lcd->dma.cfg.prime_tx && prime_count != 0u) {
      for (size_t i = 0u; i < prime_count; ++i) {
        if (!lcd->dma.cfg.prime_tx(bytes[i], lcd->cfg.user)) {
          ht5023_log_write(HT5023_LOG_LEVEL_ERROR, "st7789", "dma prime failed len=%lu", (unsigned long)len);
          dma_note_fallback(lcd);
          return false;
        }
      }
      primed = true;
    }

    if ((dma_len != 0u) && !ht5023_dma_wait(xfer.channel, lcd->dma.cfg.timeout_polls)) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "st7789",
                       "dma xfer failed chunk=%lu rem=%lu",
                       (unsigned long)chunk_len,
                       (unsigned long)len);
      log_dma_io_regs(lcd, "dma fail");
      if (rx_started) (void)ht5023_dma_wait(rx_xfer.channel, 1u);
      if (primed && lcd->cfg.write) {
        size_t dma_done = (size_t)ht5023_dma_get_transferred_count(xfer.channel);
        if (dma_done > dma_len) dma_done = dma_len;
        completed = prime_count + dma_done;
        if (lcd->dma.cfg.wait_complete) (void)lcd->dma.cfg.wait_complete(lcd->cfg.user);
        if (completed < chunk_len) {
          lcd->cfg.write(bytes + completed, chunk_len - completed, lcd->cfg.user);
          if (lcd->dma.cfg.wait_complete) (void)lcd->dma.cfg.wait_complete(lcd->cfg.user);
        }
        ht5023_log_write(HT5023_LOG_LEVEL_WARN,
                         "st7789",
                         "dma fallback remainder completed=%lu chunk=%lu",
                         (unsigned long)completed,
                         (unsigned long)chunk_len);
        dma_note_fallback(lcd);
        bytes += chunk_len;
        len -= chunk_len;
        continue;
      }
      dma_note_fallback(lcd);
      return false;
    }

    if (rx_started && !ht5023_dma_wait(rx_xfer.channel, lcd->dma.cfg.timeout_polls)) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "st7789",
                       "dma rx wait failed chunk=%lu rem=%lu",
                       (unsigned long)chunk_len,
                       (unsigned long)len);
      log_dma_io_regs(lcd, "dma rx fail");
      dma_note_fallback(lcd);
      return false;
    }

    if (!rx_started && lcd->dma.cfg.wait_complete && !lcd->dma.cfg.wait_complete(lcd->cfg.user)) {
      ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                       "st7789",
                       "spi wait_complete failed chunk=%lu rem=%lu",
                       (unsigned long)chunk_len,
                       (unsigned long)len);
      dma_note_fallback(lcd);
      return false;
    }

    dma_note_success(lcd);
    bytes += chunk_len;
    len -= chunk_len;
  }

  return true;
}

static void write_bytes(ht5023_st7789_t* lcd, const uint8_t* bytes, size_t len) {
  const bool force_dma = lcd && (lcd->cfg.write == 0);
  if (!lcd || !bytes || len == 0u) return;
  if (dma_transfer_bytes(lcd, bytes, len, force_dma)) return;
  if (lcd->cfg.write) {
    if (lcd->dma.configured && !force_dma) dma_note_fallback(lcd);
    lcd->cfg.write(bytes, len, lcd->cfg.user);
  }
}

static void write_cmd(ht5023_st7789_t* lcd, uint8_t cmd) {
  dc_write(lcd, false);
  cs_write(lcd, false);
  write_bytes(lcd, &cmd, 1u);
  cs_write(lcd, true);
}

static void write_data(ht5023_st7789_t* lcd, const uint8_t* data, size_t len) {
  if (!data || len == 0u) return;
  dc_write(lcd, true);
  cs_write(lcd, false);
  write_bytes(lcd, data, len);
  cs_write(lcd, true);
}

static void write_cmd_data(ht5023_st7789_t* lcd, uint8_t cmd, const uint8_t* data, size_t len) {
  write_cmd(lcd, cmd);
  if (len) write_data(lcd, data, len);
}

static bool dma_transfer_staged_words(ht5023_st7789_t* lcd, const uint16_t* tx_words, size_t byte_len) {
  size_t dma_len;
  size_t prime_count;
  bool rx_started = false;
  ht5023_dma_transfer_t xfer;
  ht5023_dma_transfer_t rx_xfer;

  if (!lcd || !tx_words || (byte_len == 0u)) return false;
  if (!lcd->dma.configured || !lcd->dma.enabled) return false;
  if (lcd->dma.cfg.tx_reg_addr == 0u) return false;

  prime_count = lcd->dma.cfg.prime_tx ? 1u : 0u;
  clear_spi_status(lcd);

  if (byte_len <= prime_count) {
    for (size_t i = 0u; i < byte_len; ++i) {
      if (!lcd->dma.cfg.prime_tx || !lcd->dma.cfg.prime_tx((uint8_t)tx_words[i], lcd->cfg.user)) {
        dma_note_fallback(lcd);
        return false;
      }
      if (lcd->dma.cfg.wait_complete && !lcd->dma.cfg.wait_complete(lcd->cfg.user)) {
        dma_note_fallback(lcd);
        return false;
      }
    }
    dma_note_success(lcd);
    return true;
  }

  dma_len = byte_len - prime_count;

  xfer.channel = lcd->dma.cfg.channel;
  xfer.request = lcd->dma.cfg.request;
  xfer.src_addr = (uintptr_t)(tx_words + prime_count);
  xfer.dst_addr = lcd->dma.cfg.tx_reg_addr;
  xfer.count = (uint16_t)dma_len;
  xfer.width = HT5023_DMA_WIDTH_16;
  xfer.src_inc = HT5023_DMA_ADDR_INC;
  xfer.dst_inc = HT5023_DMA_ADDR_FIXED;
  xfer.block_mode = false;
  xfer.cyclic = false;
  xfer.block_len = 0u;

  if (lcd->dma.cfg.rx_reg_addr != 0u) {
    rx_xfer.channel = lcd->dma.cfg.rx_channel;
    rx_xfer.request = lcd->dma.cfg.rx_request;
    rx_xfer.src_addr = lcd->dma.cfg.rx_reg_addr;
    rx_xfer.dst_addr = (uintptr_t)lcd->dma.cfg.rx_stage_buffer;
    rx_xfer.count = (uint16_t)dma_len;
    rx_xfer.width = HT5023_DMA_WIDTH_16;
    rx_xfer.src_inc = HT5023_DMA_ADDR_FIXED;
    rx_xfer.dst_inc = HT5023_DMA_ADDR_INC;
    rx_xfer.block_mode = false;
    rx_xfer.cyclic = false;
    rx_xfer.block_len = 0u;
    if (!ht5023_dma_start(&rx_xfer)) {
      dma_note_fallback(lcd);
      return false;
    }
    rx_started = true;
  }

  if (!ht5023_dma_start(&xfer)) {
    dma_note_fallback(lcd);
    return false;
  }

  if (prime_count != 0u) {
    if (!lcd->dma.cfg.prime_tx((uint8_t)tx_words[0], lcd->cfg.user)) {
      dma_note_fallback(lcd);
      return false;
    }
  }

  if (!ht5023_dma_wait(xfer.channel, lcd->dma.cfg.timeout_polls)) {
    if (rx_started) (void)ht5023_dma_wait(rx_xfer.channel, 1u);
    dma_note_fallback(lcd);
    return false;
  }
  if (rx_started && !ht5023_dma_wait(rx_xfer.channel, lcd->dma.cfg.timeout_polls)) {
    dma_note_fallback(lcd);
    return false;
  }
  if (!rx_started && lcd->dma.cfg.wait_complete && !lcd->dma.cfg.wait_complete(lcd->cfg.user)) {
    dma_note_fallback(lcd);
    return false;
  }

  dma_note_success(lcd);
  return true;
}

void ht5023_st7789_hard_reset(ht5023_st7789_t* lcd) {
  if (!lcd || !lcd->cfg.rst_write) return;
  lcd->cfg.rst_write(true, lcd->cfg.user);
  delay_ms(lcd, 10u);
  lcd->cfg.rst_write(false, lcd->cfg.user);
  delay_ms(lcd, 20u);
  lcd->cfg.rst_write(true, lcd->cfg.user);
  delay_ms(lcd, 120u);
}

static uint8_t madctl_for_rotation(const ht5023_st7789_t* lcd, ht5023_st7789_rotation_t rot) {
  uint8_t madctl = lcd && lcd->cfg.bgr_order ? ST7789_MADCTL_BGR : 0u;
  switch (rot) {
    case HT5023_ST7789_ROTATION_0:   madctl |= 0u; break;
    case HT5023_ST7789_ROTATION_90:  madctl |= (uint8_t)(ST7789_MADCTL_MV | ST7789_MADCTL_MX); break;
    case HT5023_ST7789_ROTATION_180: madctl |= (uint8_t)(ST7789_MADCTL_MX | ST7789_MADCTL_MY); break;
    case HT5023_ST7789_ROTATION_270: madctl |= (uint8_t)(ST7789_MADCTL_MV | ST7789_MADCTL_MY); break;
    default: break;
  }
  return madctl;
}

void ht5023_st7789_set_rotation(ht5023_st7789_t* lcd, ht5023_st7789_rotation_t rotation) {
  if (!lcd) return;
  lcd->rotation = (ht5023_st7789_rotation_t)((uint8_t)rotation & 0x03u);
  if ((lcd->rotation == HT5023_ST7789_ROTATION_90) || (lcd->rotation == HT5023_ST7789_ROTATION_270)) {
    lcd->width = lcd->cfg.height;
    lcd->height = lcd->cfg.width;
  } else {
    lcd->width = lcd->cfg.width;
    lcd->height = lcd->cfg.height;
  }
  const uint8_t madctl = madctl_for_rotation(lcd, lcd->rotation);
  write_cmd_data(lcd, ST7789_CMD_MADCTL, &madctl, 1u);
}

bool ht5023_st7789_init(ht5023_st7789_t* lcd, const ht5023_st7789_config_t* cfg) {
  if (!lcd || !cfg) return false;
  if (!cfg->dc_write) return false;
  if (!cfg->write && !cfg->dma) return false;
  if (cfg->width == 0u || cfg->height == 0u) return false;

  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "st7789",
                   "init w=%u h=%u dma=%u",
                   (unsigned)cfg->width,
                   (unsigned)cfg->height,
                   (unsigned)(cfg->dma != 0));

  memset(lcd, 0, sizeof(*lcd));
  lcd->cfg = *cfg;
  lcd->width = cfg->width;
  lcd->height = cfg->height;
  lcd->rotation = HT5023_ST7789_ROTATION_0;
  lcd->cfg.dma = 0;

  if (cfg->dma) {
    if (cfg->dma->tx_reg_addr == 0u) return false;
    if (cfg->dma->timeout_polls == 0u) return false;
    if (!cfg->dma->stage_buffer || (cfg->dma->stage_buffer_len < 2u)) return false;
    if ((((uintptr_t)cfg->dma->stage_buffer) & 0x1u) != 0u) return false;
    if (cfg->dma->rx_reg_addr != 0u) {
      if (!cfg->dma->rx_stage_buffer || (cfg->dma->rx_stage_buffer_len < 2u)) return false;
      if ((((uintptr_t)cfg->dma->rx_stage_buffer) & 0x1u) != 0u) return false;
      if (cfg->dma->rx_channel == cfg->dma->channel) return false;
    }
    lcd->dma.configured = true;
    lcd->dma.enabled = true;
    lcd->dma.cfg = *cfg->dma;
    if (lcd->dma.cfg.min_bytes == 0u) lcd->dma.cfg.min_bytes = 1u;
  }

  if (lcd->cfg.cs_write) lcd->cfg.cs_write(true, lcd->cfg.user);
  if (lcd->cfg.dc_write) lcd->cfg.dc_write(true, lcd->cfg.user);
  ht5023_st7789_hard_reset(lcd);

  write_cmd(lcd, ST7789_CMD_SWRESET);
  delay_ms(lcd, 150u);

  write_cmd(lcd, ST7789_CMD_SLPOUT);
  delay_ms(lcd, 120u);

  {
    const uint8_t colmod = 0x55u;  // RGB565
    write_cmd_data(lcd, ST7789_CMD_COLMOD, &colmod, 1u);
  }

  ht5023_st7789_set_rotation(lcd, HT5023_ST7789_ROTATION_0);

  if (lcd->cfg.invert_colors) write_cmd(lcd, ST7789_CMD_INVON);
  else write_cmd(lcd, ST7789_CMD_INVOFF);

  write_cmd(lcd, ST7789_CMD_NORON);
  delay_ms(lcd, 10u);
  write_cmd(lcd, ST7789_CMD_DISPON);
  delay_ms(lcd, 100u);

  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "st7789",
                   "init done rot=%u w=%u h=%u",
                   (unsigned)lcd->rotation,
                   (unsigned)lcd->width,
                   (unsigned)lcd->height);

  return true;
}

void ht5023_st7789_get_dma_stats(const ht5023_st7789_t* lcd, ht5023_st7789_dma_stats_t* out_stats) {
  if (!out_stats) return;
  memset(out_stats, 0, sizeof(*out_stats));
  if (!lcd) return;
  out_stats->configured = lcd->dma.configured;
  out_stats->enabled = lcd->dma.enabled;
  out_stats->success_count = lcd->dma.success_count;
  out_stats->fallback_count = lcd->dma.fallback_count;
  out_stats->fail_streak = lcd->dma.fail_streak;
}

void ht5023_st7789_reset_dma_stats(ht5023_st7789_t* lcd) {
  if (!lcd) return;
  lcd->dma.success_count = 0u;
  lcd->dma.fallback_count = 0u;
  lcd->dma.fail_streak = 0u;
  if (lcd->dma.configured) lcd->dma.enabled = true;
}

void ht5023_st7789_set_addr_window(ht5023_st7789_t* lcd,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t w,
                                   uint16_t h) {
  if (!lcd || w == 0u || h == 0u) return;

  uint16_t x0 = (uint16_t)(x + lcd->cfg.x_offset);
  uint16_t y0 = (uint16_t)(y + lcd->cfg.y_offset);
  uint16_t x1 = (uint16_t)(x0 + w - 1u);
  uint16_t y1 = (uint16_t)(y0 + h - 1u);
  uint8_t buf[4];

  buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
  buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
  write_cmd_data(lcd, ST7789_CMD_CASET, buf, sizeof(buf));

  buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
  buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
  write_cmd_data(lcd, ST7789_CMD_RASET, buf, sizeof(buf));

  write_cmd(lcd, ST7789_CMD_RAMWR);
}

void ht5023_st7789_write_pixels_rgb565(ht5023_st7789_t* lcd,
                                       const uint16_t* pixels,
                                       size_t count) {
  if (!lcd || !pixels || count == 0u) return;

  dc_write(lcd, true);
  cs_write(lcd, false);
  if (lcd->dma.configured && lcd->dma.enabled) {
    uint16_t* stage_words = (uint16_t*)lcd->dma.cfg.stage_buffer;
    const size_t stage_bytes = dma_stage_capacity_bytes(lcd);
    if (stage_words && (stage_bytes >= 2u)) {
      const size_t max_pixels = stage_bytes / 2u;
      while (count) {
        size_t n_pixels = count;
        if (n_pixels > max_pixels) n_pixels = max_pixels;
        for (size_t i = 0u; i < n_pixels; ++i) {
          const uint16_t px = pixels[i];
          stage_words[(i * 2u) + 0u] = (uint16_t)(px >> 8);
          stage_words[(i * 2u) + 1u] = (uint16_t)(px & 0xFFu);
        }
        if (!dma_transfer_staged_words(lcd, stage_words, n_pixels * 2u)) break;
        pixels += n_pixels;
        count -= n_pixels;
      }
      if (count == 0u) {
        cs_write(lcd, true);
        return;
      }
    }
  }
  while (count) {
    uint8_t chunk[ST7789_STREAM_CHUNK_BYTES];
    size_t n_pixels = count;
    if (n_pixels > (sizeof(chunk) / 2u)) n_pixels = sizeof(chunk) / 2u;
    for (size_t i = 0; i < n_pixels; ++i) {
      const uint16_t px = pixels[i];
      chunk[(i * 2u) + 0u] = (uint8_t)(px >> 8);
      chunk[(i * 2u) + 1u] = (uint8_t)px;
    }
    write_bytes(lcd, chunk, n_pixels * 2u);
    pixels += n_pixels;
    count -= n_pixels;
  }
  cs_write(lcd, true);
}

void ht5023_st7789_write_color_rgb565(ht5023_st7789_t* lcd, uint16_t color, size_t count) {
  if (!lcd || count == 0u) return;
  dc_write(lcd, true);
  cs_write(lcd, false);
  if (lcd->dma.configured && lcd->dma.enabled) {
    uint16_t* stage_words = (uint16_t*)lcd->dma.cfg.stage_buffer;
    const size_t stage_bytes = dma_stage_capacity_bytes(lcd);
    if (stage_words && (stage_bytes >= 2u)) {
      const size_t max_pixels = stage_bytes / 2u;
      while (count) {
        size_t n_pixels = count;
        if (n_pixels > max_pixels) n_pixels = max_pixels;
        for (size_t i = 0u; i < n_pixels; ++i) {
          stage_words[(i * 2u) + 0u] = (uint16_t)(color >> 8);
          stage_words[(i * 2u) + 1u] = (uint16_t)(color & 0xFFu);
        }
        if (!dma_transfer_staged_words(lcd, stage_words, n_pixels * 2u)) break;
        count -= n_pixels;
      }
      if (count == 0u) {
        cs_write(lcd, true);
        return;
      }
    }
  }
  while (count) {
    uint8_t chunk[ST7789_STREAM_CHUNK_BYTES];
    size_t n_pixels = count;
    if (n_pixels > (sizeof(chunk) / 2u)) n_pixels = sizeof(chunk) / 2u;
    for (size_t i = 0; i < n_pixels; ++i) {
      chunk[(i * 2u) + 0u] = (uint8_t)(color >> 8);
      chunk[(i * 2u) + 1u] = (uint8_t)color;
    }
    write_bytes(lcd, chunk, n_pixels * 2u);
    count -= n_pixels;
  }
  cs_write(lcd, true);
}

void ht5023_st7789_fill_rect(ht5023_st7789_t* lcd,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t color) {
  if (!lcd || w == 0u || h == 0u) return;
  if (x >= lcd->width || y >= lcd->height) return;
  if ((uint32_t)x + (uint32_t)w > lcd->width) w = (uint16_t)(lcd->width - x);
  if ((uint32_t)y + (uint32_t)h > lcd->height) h = (uint16_t)(lcd->height - y);

  ht5023_st7789_set_addr_window(lcd, x, y, w, h);
  ht5023_st7789_write_color_rgb565(lcd, color, (size_t)w * (size_t)h);
}

void ht5023_st7789_fill_screen(ht5023_st7789_t* lcd, uint16_t color) {
  if (!lcd) return;
  ht5023_st7789_fill_rect(lcd, 0u, 0u, lcd->width, lcd->height, color);
}
