// ht5023_uart.h — UART driver for HT5023/HT502X
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_UART_ID_0 = 0,
  HT5023_UART_ID_1 = 1,
  HT5023_UART_ID_2 = 2,
  HT5023_UART_ID_3 = 3,
  HT5023_UART_ID_4 = 4,
  HT5023_UART_ID_5 = 5,
} ht5023_uart_id_t;

typedef struct {
  uint32_t sysclk_hz;   // e.g. 10 MHz default HRC
  uint32_t baud;        // e.g. 9600 or 115200
  bool enable_tx;
  bool enable_rx;
} ht5023_uart_config_t;

// Datasheet pin map helper for UART0 on PC.3(TX0)/PC.2(RX0):
// set IOCFG=1 and AFCFG=0 on PC2/PC3, and enable RX pull-up.
void ht5023_uart0_pins_init_pc2_pc3(void);
// Datasheet pin map helper for UART1 on PC.0(TX1)/PC.1(RX1):
// set IOCFG=1 and AFCFG=0 on PC0/PC1 (UART1 is the first mux function, the
// second being LCD segments SEG43/SEG44), and enable RX pull-up.
void ht5023_uart1_pins_init_pc0_pc1(void);
// Datasheet pin map helper for UART2 on PC.11(TX2)/PC.12(RX2):
// set IOCFG=1 on PC11/PC12 (AFCFG is don't-care for this pair; both mux slots are UART2).
void ht5023_uart2_pins_init_pc11_pc12(void);
// Datasheet pin map helper for UART5 on PC.5(TX5)/PC.4(RX5):
// set IOCFG=1 and AFCFG=1 on PC4/PC5 (UART5 is the second mux function, the
// first being SPI_MOSI/SPI_MISO, so SPI cannot be used at the same time), and
// enable RX pull-up.
void ht5023_uart5_pins_init_pc4_pc5(void);

bool ht5023_uart_init(ht5023_uart_id_t id, const ht5023_uart_config_t* cfg);

void ht5023_uart_write_byte(ht5023_uart_id_t id, uint8_t byte);
void ht5023_uart_write(ht5023_uart_id_t id, const void* data, size_t len);
void ht5023_uart_write_str(ht5023_uart_id_t id, const char* s);

bool ht5023_uart_rx_ready(ht5023_uart_id_t id);
bool ht5023_uart_try_read_byte(ht5023_uart_id_t id, uint8_t* out);
uint8_t ht5023_uart_read_byte_blocking(ht5023_uart_id_t id);

#ifdef __cplusplus
}
#endif
