#pragma once

#include "Stream.h"
#include "ht5023_uart.h"

#define SERIAL_8N1 0x06u

class HardwareSerial : public Stream {
 public:
  explicit HardwareSerial(ht5023_uart_id_t uart_id);

  void begin(unsigned long baud);
  void begin(unsigned long baud, uint16_t config);
  void end(void);

  int available(void) override;
  int peek(void) override;
  int read(void) override;
  void flush(void) override;

  size_t write(uint8_t byte) override;
  using Print::write;

  explicit operator bool() const { return true; }

 private:
  bool initPort(unsigned long baud);
  bool rxReady(void) const;
  bool tryReadByte(uint8_t* value);

  ht5023_uart_id_t uart_id_;
  bool initialized_ = false;
  int peeked_ = -1;
};

extern HardwareSerial Serial0;
extern HardwareSerial Serial2;
extern HardwareSerial& Serial;
