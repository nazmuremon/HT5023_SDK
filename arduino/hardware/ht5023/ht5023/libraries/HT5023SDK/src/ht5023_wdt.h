// ht5023_wdt.h — HT5023 watchdog helper
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// WDT timeout formula from datasheet: timeout_ms = 64 * (1 + SET[7:0])
// Minimum timeout is 64 ms (SET=0); maximum is ~16.4 s (SET=255).
uint8_t ht5023_wdt_set_from_timeout_ms(uint32_t timeout_ms);
uint32_t ht5023_wdt_timeout_ms_from_set(uint8_t set_value);

// Configure timeout (SET[7:0]) and feed in one write.
void ht5023_wdt_configure_and_feed(uint8_t set_value);

// Feed watchdog using the currently configured timeout value.
void ht5023_wdt_feed(void);

// Read current WDT configuration/counter.
uint8_t ht5023_wdt_get_set_value(void);
uint16_t ht5023_wdt_get_counter(void);

#ifdef __cplusplus
}
#endif
