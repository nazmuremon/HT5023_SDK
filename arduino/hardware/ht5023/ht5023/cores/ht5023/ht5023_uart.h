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
  uint32_t sysclk_hz;
  uint32_t baud;
  bool enable_tx;
  bool enable_rx;
} ht5023_uart_config_t;

void ht5023_uart0_pins_init_pc2_pc3(void);
void ht5023_uart2_pins_init_pc11_pc12(void);
bool ht5023_uart_init(ht5023_uart_id_t id, const ht5023_uart_config_t* cfg);
bool ht5023_uart2_init(const ht5023_uart_config_t* cfg);
void ht5023_uart_write_byte(ht5023_uart_id_t id, uint8_t byte);
void ht5023_uart_write(ht5023_uart_id_t id, const void* data, size_t len);
void ht5023_uart_write_str(ht5023_uart_id_t id, const char* s);
void ht5023_uart2_write_byte(uint8_t byte);
void ht5023_uart2_write(const void* data, size_t len);
bool ht5023_uart_rx_ready(ht5023_uart_id_t id);
bool ht5023_uart_try_read_byte(ht5023_uart_id_t id, uint8_t* out);
uint8_t ht5023_uart_read_byte_blocking(ht5023_uart_id_t id);
bool ht5023_uart2_rx_ready(void);
bool ht5023_uart2_try_read_byte(uint8_t* out);
uint8_t ht5023_uart2_read_byte_blocking(void);

#ifdef __cplusplus
}
#endif
