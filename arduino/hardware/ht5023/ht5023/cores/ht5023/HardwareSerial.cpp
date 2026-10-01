#include "HardwareSerial.h"

#include "Arduino.h"

HardwareSerial Serial0(HT5023_UART_ID_0);
HardwareSerial Serial2(HT5023_UART_ID_2);
HardwareSerial& Serial = Serial2;

HardwareSerial::HardwareSerial(ht5023_uart_id_t uart_id) : uart_id_(uart_id) {
}

bool HardwareSerial::initPort(unsigned long baud) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = (uint32_t)F_CPU,
    .baud = (uint32_t)baud,
    .enable_tx = true,
    .enable_rx = true,
  };

  switch (uart_id_) {
    case HT5023_UART_ID_0:
      ht5023_uart0_pins_init_pc2_pc3();
      return ht5023_uart_init(HT5023_UART_ID_0, &uart_cfg);
    case HT5023_UART_ID_2:
      return ht5023_uart2_init(&uart_cfg);
    default:
      return ht5023_uart_init(uart_id_, &uart_cfg);
  }
}

bool HardwareSerial::rxReady(void) const {
  switch (uart_id_) {
    case HT5023_UART_ID_2:
      return ht5023_uart2_rx_ready();
    default:
      return ht5023_uart_rx_ready(uart_id_);
  }
}

bool HardwareSerial::tryReadByte(uint8_t* value) {
  if (!value) return false;

  switch (uart_id_) {
    case HT5023_UART_ID_2:
      return ht5023_uart2_try_read_byte(value);
    default:
      return ht5023_uart_try_read_byte(uart_id_, value);
  }
}

void HardwareSerial::begin(unsigned long baud) {
  begin(baud, SERIAL_8N1);
}

void HardwareSerial::begin(unsigned long baud, uint16_t config) {
  (void)config;
  initialized_ = initPort(baud);
  peeked_ = -1;
}

void HardwareSerial::end(void) {
  initialized_ = false;
  peeked_ = -1;
}

int HardwareSerial::available(void) {
  if (!initialized_) return 0;
  if (peeked_ >= 0) return 1;
  return rxReady() ? 1 : 0;
}

int HardwareSerial::peek(void) {
  if (!initialized_) return -1;
  if (peeked_ >= 0) return peeked_;

  uint8_t value = 0u;
  if (!tryReadByte(&value)) return -1;
  peeked_ = value;
  return peeked_;
}

int HardwareSerial::read(void) {
  if (!initialized_) return -1;
  if (peeked_ >= 0) {
    const int value = peeked_;
    peeked_ = -1;
    return value;
  }

  uint8_t value = 0u;
  if (!tryReadByte(&value)) return -1;
  return value;
}

void HardwareSerial::flush(void) {
}

size_t HardwareSerial::write(uint8_t byte) {
  if (!initialized_) begin(9600u);
  if (initialized_) ht5023_uart_write_byte(uart_id_, byte);
  return 1u;
}

void serialEvent(void) __attribute__((weak));
void serialEvent(void) {
}

void serialEventRun(void) {
  if (Serial.available() > 0) serialEvent();
}
