#include "Wire.h"

#include "ht5023_i2c.h"

TwoWire Wire;

TwoWire::TwoWire() = default;

void TwoWire::begin(void) {
  const ht5023_i2c_master_cfg_t cfg = {
      .sysclk_hz = (uint32_t)F_CPU,
      .bus_hz = clock_hz_,
  };
  begun_ = ht5023_i2c_begin(&cfg);
}

void TwoWire::begin(uint8_t address) {
  (void)address;
  begin();
}

void TwoWire::end(void) {
  begun_ = false;
}

void TwoWire::setClock(uint32_t clock_hz) {
  if (clock_hz == 0u) return;
  clock_hz_ = clock_hz;
  if (begun_) begin();
}

void TwoWire::beginTransmission(uint8_t address) {
  tx_address_ = address;
  tx_length_ = 0u;
  pending_restart_ = false;
}

uint8_t TwoWire::endTransmission(uint8_t send_stop) {
  if (!begun_) begin();

  if (!send_stop) {
    pending_restart_ = true;
    return 0u;
  }

  const bool ok = ht5023_i2c_write(tx_address_, tx_buffer_, tx_length_);
  tx_length_ = 0u;
  pending_restart_ = false;
  return ok ? 0u : 4u;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t send_stop) {
  (void)send_stop;
  return (uint8_t)requestFrom((int)address, (int)quantity, (int)send_stop);
}

size_t TwoWire::requestFrom(int address, int quantity, int send_stop) {
  (void)send_stop;
  if (quantity <= 0) return 0u;
  if (!begun_) begin();

  const size_t wanted = (size_t)((quantity > (int)kBufferLength) ? kBufferLength : quantity);
  bool ok = false;

  if (pending_restart_ && tx_length_ > 0u && (uint8_t)address == tx_address_) {
    ok = ht5023_i2c_write_read((uint8_t)address, tx_buffer_, tx_length_, rx_buffer_, wanted);
  } else {
    ok = ht5023_i2c_read((uint8_t)address, rx_buffer_, wanted);
  }

  tx_length_ = 0u;
  pending_restart_ = false;
  rx_index_ = 0u;
  peeked_ = -1;
  rx_length_ = ok ? wanted : 0u;
  return rx_length_;
}

int TwoWire::available(void) {
  return (int)(rx_length_ - rx_index_) + (peeked_ >= 0 ? 1 : 0);
}

int TwoWire::read(void) {
  if (peeked_ >= 0) {
    const int value = peeked_;
    peeked_ = -1;
    return value;
  }
  if (rx_index_ >= rx_length_) return -1;
  return rx_buffer_[rx_index_++];
}

int TwoWire::peek(void) {
  if (peeked_ >= 0) return peeked_;
  if (rx_index_ >= rx_length_) return -1;
  peeked_ = rx_buffer_[rx_index_++];
  return peeked_;
}

void TwoWire::flush(void) {
}

size_t TwoWire::write(uint8_t value) {
  if (tx_length_ >= kBufferLength) return 0u;
  tx_buffer_[tx_length_++] = value;
  return 1u;
}

size_t TwoWire::write(const uint8_t* data, size_t length) {
  if (!data) return 0u;

  size_t written = 0u;
  while (written < length && tx_length_ < kBufferLength) {
    tx_buffer_[tx_length_++] = data[written++];
  }
  return written;
}

void TwoWire::onReceive(void (*function)(int)) {
  (void)function;
}

void TwoWire::onRequest(void (*function)(void)) {
  (void)function;
}
