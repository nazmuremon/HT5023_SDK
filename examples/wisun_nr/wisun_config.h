// wisun_config.h — every tunable for the Wi-SUN node (NR) firmware.
//
// Nothing else in examples/wisun_nr hard-codes a pin, timeout, size or network
// parameter. Everything here can also be overridden from the command line:
//
//   make EXAMPLE=wisun_nr EXTRA_CFLAGS="-DWISUN_LOG_UART_TRAFFIC=0"
#pragma once

// ===========================================================================
// Hardware (HT5023 platform layer, hal_ht5023.c)
// ===========================================================================

// UART wired to the EBYTE E51. 3.3 V logic, common GND. Supported values:
//   5 = UART5: PC5 (TX5 -> E51 RXD), PC4 (RX5 <- E51 TXD)   <- default
//   1 = UART1: PC0 (TX1 -> E51 RXD), PC1 (RX1 <- E51 TXD)   (examples/wisun_get's wiring)
//   0 = UART0: PC3 (TX0 -> E51 RXD), PC2 (RX0 <- E51 TXD)
// PC4/PC5 are also SPI_MOSI/SPI_MISO: with UART5 the SPI bus is unavailable.
#ifndef WISUN_MODULE_UART
#define WISUN_MODULE_UART 5
#endif

#ifndef WISUN_MODULE_BAUD
#define WISUN_MODULE_BAUD 115200u
#endif

// Debug log and console. 2 = UART2 on PC11/PC12, the USB-UART the Flasher's
// log and uploads use.
#ifndef WISUN_LOG_UART
#define WISUN_LOG_UART 2
#endif

#ifndef WISUN_LOG_BAUD
#define WISUN_LOG_BAUD 115200u
#endif

// Fsys, the clock the UART dividers are computed from (SysTick derives Fcpu
// from it and the live SYSCLK_DIV register). Defaults to the SDK-wide
// HT5023_FSYS_HZ in ht5023/core/system.h: 9.196 MHz as measured on the board,
// not the datasheet's 9.5 MHz (-0.22 % vs -2.65 % baud error at 115200).
#ifndef WISUN_SYSCLK_HZ
#define WISUN_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// Optional hardware reset of the E51. 0 = not wired (module_reset_pulse is a
// no-op). Check the reset pin name and its active level in the EBYTE E51
// manual before enabling -- nothing here assumes them.
#ifndef WISUN_RESET_ENABLED
#define WISUN_RESET_ENABLED 0
#endif
#ifndef WISUN_RESET_PORT          // 0=PA 1=PB 2=PC 3=PD 4=PE 5=PF
#define WISUN_RESET_PORT 4
#endif
#ifndef WISUN_RESET_PIN
#define WISUN_RESET_PIN 4
#endif
#ifndef WISUN_RESET_ACTIVE_LOW
#define WISUN_RESET_ACTIVE_LOW 1
#endif
#ifndef WISUN_RESET_PULSE_MS
#define WISUN_RESET_PULSE_MS 100u
#endif

// ===========================================================================
// Network parameters -- must be identical on the border router (spec section 4)
// ===========================================================================

#ifndef WISUN_NETWORK_NAME
#define WISUN_NETWORK_NAME "Wi-SUN Network"
#endif
#ifndef WISUN_REGULATORY_DOMAIN       // must be legal where the node is deployed
#define WISUN_REGULATORY_DOMAIN "NA"
#endif
#ifndef WISUN_CHAN_PLAN_ID
#define WISUN_CHAN_PLAN_ID 1
#endif
#ifndef WISUN_PHY_MODE_ID
#define WISUN_PHY_MODE_ID 2
#endif
#ifndef WISUN_APP_PORT
#define WISUN_APP_PORT 9000u
#endif

// 1 = settings on the module that differ from the four above are set and saved
//     (spec section 6, APPLY_CONFIG).
// 0 = never change them: log the difference and stay in ERROR. Use it to look
//     at a module configured for another network before letting the firmware
//     rewrite its saved settings.
#ifndef WISUN_APPLY_CONFIG
#define WISUN_APPLY_CONFIG 1
#endif

// ===========================================================================
// CLI driver timeouts (ms)
// ===========================================================================

#define WISUN_CMD_TIMEOUT_MS          3000u
#define WISUN_CMD_GET_TIMEOUT_MS      4000u
#define WISUN_CMD_SOCKET_TIMEOUT_MS   5000u
#define WISUN_CMD_JOIN_TIMEOUT_MS     5000u

// ===========================================================================
// State machine timing (ms)
// ===========================================================================

#define WISUN_SYNC_PERIOD_MS          1000u     // "\r\n" while looking for "> "
#define WISUN_SYNC_TIMEOUT_MS         15000u
#define WISUN_DISCONNECT_WAIT_MS      5000u
#define WISUN_JOIN_TIMEOUT_MS         360000u   // give up after this long with no join-state progress
#define WISUN_JOIN_MAX_MS             900000u   // ... or after this long in total, progress or not
#define WISUN_JOIN_POLL_MS            15000u    // backup "get wisun" while joining
#define WISUN_BACKOFF_FIRST_MS        10000u
#define WISUN_BACKOFF_MAX_MS          120000u
#define WISUN_BR_ADDR_RETRY_MS        5000u
#define WISUN_BR_ADDR_GIVEUP_MS       60000u
#define WISUN_PING_WAIT_MS            10000u
#define WISUN_PING_GAP_MS             5000u
#define WISUN_PING_TRIES              3u
#define WISUN_OPENED_WAIT_MS          15000u
#define WISUN_CONNECT_RETRY_MS        10000u
// Each failed uplink (no [Opened], or the BR never connects back) leaves a socket
// on the BR's module, and it has room for only a handful. Successive failures
// therefore wait longer, up to this cap.
#define WISUN_CONNECT_BACKOFF_MAX_MS  120000u
#define WISUN_CONNECT_FAILS_RECHECK   5u
#define WISUN_ACCEPT_WAIT_MS          30000u
#define WISUN_ACCEPT_RETRY_MS         5000u
#define WISUN_HEALTH_PERIOD_MS        60000u
#define WISUN_READ_SAFETY_POLL_MS     10000u
#define WISUN_ERROR_LOG_PERIOD_MS     5000u
#define WISUN_ERROR_RETRY_MS          60000u

// ===========================================================================
// Application frame protocol (spec section 7)
// ===========================================================================

#define WISUN_FRAME_MAX_PAYLOAD       64u       // raw bytes per frame
#define WISUN_FRAME_MAX_CHARS         200u      // drop longer text without ';'
#define WISUN_MSG_MAX                 1024u     // reassembled message ceiling
#define WISUN_ACK_TIMEOUT_MS          5000u
#define WISUN_ACK_RETRIES             3u        // resends after the first send
#define WISUN_ACK_FAILS_LINK_LOST     2u        // consecutive exhausted messages
#define WISUN_HEARTBEAT_MS            30000u
#define WISUN_HEARTBEAT_MISSES        3u

// ===========================================================================
// Buffers (all static; see the RAM budget in README.md)
// ===========================================================================

#define WISUN_UART_RX_RING            4096u     // "get wisun" is ~2 KB in one burst
#define WISUN_UART_TX_RING            512u
#define WISUN_LOG_TX_RING             2048u
#define WISUN_CONSOLE_RX_RING         128u
#define WISUN_CONSOLE_LINE_MAX        160u
#define WISUN_LINE_MAX                1100u     // socket_read returns up to 1024 on one line
#define WISUN_CMD_MAX                 192u      // spec: keep command lines <= 180 chars
#define WISUN_CMD_QUEUE_DEPTH         6u
#define WISUN_SOCKET_READ_MAX         1024u
#define WISUN_APP_QUEUE_DEPTH         8u
#define WISUN_ACK_QUEUE_DEPTH         8u
#define WISUN_MAX_SOCKETS_LISTED      16u
#define WISUN_IPV6_MAX                46u

// ===========================================================================
// Demo application and logging
// ===========================================================================

#define WISUN_DEMO_PERIOD_MS          10000u

// 1 = the log port prints only application payloads -- "RX: <payload>",
//     "TX: <payload>" once the BR acknowledges it, "TX FAILED: <payload>" -- plus
//     replies to console commands you type (status, raw, ...).
// 0 = full diagnostics: "[NR] <ms>" lines for every state change, event and
//     (with WISUN_LOG_UART_TRAFFIC) every module line.
#ifndef WISUN_LOG_PAYLOAD_ONLY
#define WISUN_LOG_PAYLOAD_ONLY 1
#endif

#ifndef WISUN_LOG_UART_TRAFFIC        // TX> / RX< lines for every module byte line (diagnostic)
#define WISUN_LOG_UART_TRAFFIC 1
#endif
