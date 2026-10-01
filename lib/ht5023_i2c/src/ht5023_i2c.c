// ht5023_i2c.c — minimal HT5023 I2C master polling driver
#include "ht5023_i2c.h"

#include <stddef.h>
#include <stdint.h>

#define HT5023_CMU_BASE               0x4000F000u
#define HT5023_CMU_WPREG_OFFSET       0x00u
#define HT5023_CMU_CLKCTRL0_OFFSET    0x2Cu
#define HT5023_WPREG_UNLOCK_KEY       0xA55Au

#define CLKCTRL0_I2C_EN_BIT           (1u << 3)

#define HT5023_GPIOC_BASE             0x40011200u
#define GPIO_IOCFG_OFFSET             0x00u
#define GPIO_AFCFG_OFFSET             0x04u
#define GPIO_PTUP_OFFSET              0x0Cu

#define HT5023_I2C_BASE               0x4000A000u
#define I2CDAT_OFFSET                 0x00u
#define I2CADR_OFFSET                 0x04u
#define I2CCON_OFFSET                 0x08u
#define I2CSTA_OFFSET                 0x0Cu

// I2CCON bits (datasheet 15.5): CR[9:3] at bits15:9, CR2 at bit7, CR[1:0] at bits1:0.
#define I2CCON_ENS1_BIT               (1u << 6)
#define I2CCON_STA_BIT                (1u << 5)
#define I2CCON_STO_BIT                (1u << 4)
#define I2CCON_SI_BIT                 (1u << 3)
#define I2CCON_AA_BIT                 (1u << 2)
#define I2CSTA_CODE_MASK              0x00F8u

#define I2C_STATUS_START              0x08u
#define I2C_STATUS_RESTART            0x10u
#define I2C_STATUS_SLAW_ACK           0x18u
#define I2C_STATUS_SLAW_NACK          0x20u
#define I2C_STATUS_DATAW_ACK          0x28u
#define I2C_STATUS_DATAW_NACK         0x30u
#define I2C_STATUS_ARB_LOST           0x38u
#define I2C_STATUS_SLAR_ACK           0x40u
#define I2C_STATUS_SLAR_NACK          0x48u
#define I2C_STATUS_DATAR_ACK          0x50u
#define I2C_STATUS_DATAR_NACK         0x58u
#define I2C_STATUS_IDLE               0xF8u

// Small polling timeout to avoid dead loops if bus is stuck.
#define I2C_POLL_TIMEOUT              60000u

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static uint16_t i2ccon_shadow = 0x0000u;

static void i2c_enable_clock(void) {
  volatile uint16_t* wpreg = reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET);
  volatile uint16_t* clk0  = reg16(HT5023_CMU_BASE, HT5023_CMU_CLKCTRL0_OFFSET);
  *wpreg = HT5023_WPREG_UNLOCK_KEY;
  *clk0 = (uint16_t)(*clk0 | (uint16_t)CLKCTRL0_I2C_EN_BIT);
  *wpreg = 0x0000u;
}

static uint16_t i2c_pack_cr(uint16_t reg, uint16_t cr10) {
  reg &= (uint16_t)~(0xFE83u);  // clear CR[9:3], CR2, CR[1:0]
  reg |= (uint16_t)(((cr10 >> 3) & 0x7Fu) << 9);
  reg |= (uint16_t)(((cr10 >> 2) & 0x1u) << 7);
  reg |= (uint16_t)(cr10 & 0x3u);
  return reg;
}

static void i2c_write_con(void) {
  *reg16(HT5023_I2C_BASE, I2CCON_OFFSET) = i2ccon_shadow;
}

static void i2c_set_bits(uint16_t set_mask) {
  i2ccon_shadow = (uint16_t)(i2ccon_shadow | set_mask);
  i2c_write_con();
}

static void i2c_clear_bits(uint16_t clr_mask) {
  i2ccon_shadow = (uint16_t)(i2ccon_shadow & (uint16_t)~clr_mask);
  i2c_write_con();
}

static bool i2c_wait_si(uint8_t* status_out) {
  volatile uint16_t* con = reg16(HT5023_I2C_BASE, I2CCON_OFFSET);
  volatile uint16_t* sta = reg16(HT5023_I2C_BASE, I2CSTA_OFFSET);
  for (uint32_t i = 0; i < I2C_POLL_TIMEOUT; ++i) {
    if (((*con) & I2CCON_SI_BIT) != 0u) {
      if (status_out) *status_out = (uint8_t)(*sta & I2CSTA_CODE_MASK);
      return true;
    }
  }
  return false;
}

static bool i2c_start_and_wait(bool repeated, uint8_t* status) {
  i2c_set_bits(I2CCON_STA_BIT);
  i2c_clear_bits(I2CCON_SI_BIT);
  if (!i2c_wait_si(status)) return false;
  const uint8_t expected = repeated ? I2C_STATUS_RESTART : I2C_STATUS_START;
  if (*status != expected) return false;
  i2c_clear_bits(I2CCON_STA_BIT);
  return true;
}

static bool i2c_send_byte_expect(uint8_t byte, uint8_t ok_status, uint8_t* got) {
  *reg16(HT5023_I2C_BASE, I2CDAT_OFFSET) = byte;
  i2c_clear_bits(I2CCON_SI_BIT);
  if (!i2c_wait_si(got)) return false;
  return *got == ok_status;
}

static void i2c_send_stop(void) {
  i2c_set_bits(I2CCON_STO_BIT);
  i2c_clear_bits(I2CCON_SI_BIT);
  // STO clears by hardware; no hard wait needed for these demos.
}

void ht5023_i2c_pins_init_pc13_pc14(void) {
  const uint16_t pins = (uint16_t)((1u << 13) | (1u << 14));
  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptup  = reg16(HT5023_GPIOC_BASE, GPIO_PTUP_OFFSET);

  *iocfg |= pins;            // multiplex function
  *afcfg &= (uint16_t)~pins; // pin table shows SCL/SDA on first function
  *ptup &= (uint16_t)~pins;  // enable internal pull-ups on digital inputs
}

bool ht5023_i2c_master_init(const ht5023_i2c_master_cfg_t* cfg) {
  if (!cfg || cfg->sysclk_hz == 0u || cfg->bus_hz == 0u) return false;

  uint32_t div = cfg->sysclk_hz / (cfg->bus_hz * 4u);
  if (div == 0u) div = 1u;
  uint32_t cr = div - 1u;
  if (cr > 0x03FFu) cr = 0x03FFu;

  i2c_enable_clock();
  ht5023_i2c_pins_init_pc13_pc14();

  // Default local slave address not used by these master demos.
  *reg16(HT5023_I2C_BASE, I2CADR_OFFSET) = 0x0000u;

  i2ccon_shadow = 0x0000u;
  i2ccon_shadow = i2c_pack_cr(i2ccon_shadow, (uint16_t)cr);
  i2ccon_shadow |= I2CCON_ENS1_BIT;
  i2c_write_con();
  return true;
}

bool ht5023_i2c_probe_7bit(uint8_t addr7) {
  uint8_t st = 0u;
  if (!i2c_start_and_wait(false, &st)) return false;

  const uint8_t sla_w = (uint8_t)((addr7 << 1) | 0u);
  bool ack = i2c_send_byte_expect(sla_w, I2C_STATUS_SLAW_ACK, &st);
  if (!ack && st != I2C_STATUS_SLAW_NACK && st != I2C_STATUS_ARB_LOST) {
    // still stop on unexpected state
  }
  i2c_send_stop();
  return ack;
}

bool ht5023_i2c_write(uint8_t addr7, const uint8_t* data, size_t len) {
  if (!data && len) return false;
  uint8_t st = 0u;
  if (!i2c_start_and_wait(false, &st)) return false;
  if (!i2c_send_byte_expect((uint8_t)(addr7 << 1), I2C_STATUS_SLAW_ACK, &st)) {
    i2c_send_stop();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!i2c_send_byte_expect(data[i], I2C_STATUS_DATAW_ACK, &st)) {
      i2c_send_stop();
      return false;
    }
  }
  i2c_send_stop();
  return true;
}

bool ht5023_i2c_read(uint8_t addr7, uint8_t* data, size_t len) {
  if (!data && len) return false;
  uint8_t st = 0u;
  if (!i2c_start_and_wait(false, &st)) return false;
  if (!i2c_send_byte_expect((uint8_t)((addr7 << 1) | 1u), I2C_STATUS_SLAR_ACK, &st)) {
    i2c_send_stop();
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    if (i + 1u < len) i2c_set_bits(I2CCON_AA_BIT);
    else i2c_clear_bits(I2CCON_AA_BIT);

    i2c_clear_bits(I2CCON_SI_BIT);
    if (!i2c_wait_si(&st)) {
      i2c_send_stop();
      return false;
    }

    if (i + 1u < len) {
      if (st != I2C_STATUS_DATAR_ACK) { i2c_send_stop(); return false; }
    } else {
      if (st != I2C_STATUS_DATAR_NACK) { i2c_send_stop(); return false; }
    }
    data[i] = (uint8_t)(*reg16(HT5023_I2C_BASE, I2CDAT_OFFSET) & 0xFFu);
  }

  i2c_send_stop();
  return true;
}


static bool i2c_read_phase(uint8_t addr7, uint8_t* data, size_t len) {
  uint8_t st = 0u;
  if (!i2c_start_and_wait(true, &st)) return false;
  if (!i2c_send_byte_expect((uint8_t)((addr7 << 1) | 1u), I2C_STATUS_SLAR_ACK, &st)) {
    i2c_send_stop();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (i + 1u < len) i2c_set_bits(I2CCON_AA_BIT);
    else i2c_clear_bits(I2CCON_AA_BIT);

    i2c_clear_bits(I2CCON_SI_BIT);
    if (!i2c_wait_si(&st)) { i2c_send_stop(); return false; }

    if (i + 1u < len) {
      if (st != I2C_STATUS_DATAR_ACK)  { i2c_send_stop(); return false; }
    } else {
      if (st != I2C_STATUS_DATAR_NACK) { i2c_send_stop(); return false; }
    }
    data[i] = (uint8_t)(*reg16(HT5023_I2C_BASE, I2CDAT_OFFSET) & 0xFFu);
  }
  return true;
}

bool ht5023_i2c_write_read(uint8_t addr7,
                           const uint8_t* wr_data, size_t wr_len,
                           uint8_t*       rd_data, size_t rd_len) {
  if (!wr_data && wr_len) return false;
  if (!rd_data && rd_len) return false;

  uint8_t st = 0u;

  // START + write phase
  if (!i2c_start_and_wait(false, &st)) return false;
  if (!i2c_send_byte_expect((uint8_t)(addr7 << 1), I2C_STATUS_SLAW_ACK, &st)) {
    i2c_send_stop();
    return false;
  }
  for (size_t i = 0; i < wr_len; ++i) {
    if (!i2c_send_byte_expect(wr_data[i], I2C_STATUS_DATAW_ACK, &st)) {
      i2c_send_stop();
      return false;
    }
  }

  // Repeated START + read phase (if read bytes are requested)
  if (rd_len > 0u) {
    if (!i2c_read_phase(addr7, rd_data, rd_len)) return false;
  }

  i2c_send_stop();
  return true;
}
