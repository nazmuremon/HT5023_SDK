#pragma once

#include <Arduino.h>

class TwoWire : public Stream {
 public:
  TwoWire();

  void begin(void);
  void begin(uint8_t address);
  void end(void);
  void setClock(uint32_t clock_hz);

  void beginTransmission(uint8_t address);
  uint8_t endTransmission(uint8_t send_stop = true);

  uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t send_stop = true);
  size_t requestFrom(int address, int quantity, int send_stop = true);

  int available(void) override;
  int read(void) override;
  int peek(void) override;
  void flush(void) override;

  size_t write(uint8_t value) override;
  size_t write(const uint8_t* data, size_t length) override;
  using Print::write;

  void onReceive(void (*function)(int));
  void onRequest(void (*function)(void));

 private:
  static constexpr size_t kBufferLength = 32u;

  uint8_t tx_address_ = 0u;
  uint8_t tx_buffer_[kBufferLength] = {};
  size_t tx_length_ = 0u;

  uint8_t rx_buffer_[kBufferLength] = {};
  size_t rx_length_ = 0u;
  size_t rx_index_ = 0u;
  int peeked_ = -1;

  bool pending_restart_ = false;
  uint32_t clock_hz_ = 100000u;
  bool begun_ = false;
};

extern TwoWire Wire;
