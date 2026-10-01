# SDK Architecture and Conventions

## Design rules
- Layering: core → drivers/utils → board support → applications/examples
- Drivers do not assume board pins unless exposed as explicit convenience helpers
- Public headers use a stable namespace path: `ht5023/...`
- Examples may use board support wrappers for pin mappings
- Keep peripheral register access local to drivers and core support
- Prefer polling APIs first; interrupt/DMA variants are separate functions
- All public headers wrap declarations in `extern "C"` for C++ compatibility

## Register access
- Use `ht5023_reg16()` / `ht5023_reg32()` from `ht5023/core/device.h` for all
  memory-mapped I/O.  These helpers return a `volatile` pointer with the correct
  integer type and a `uintptr_t` intermediate cast, avoiding pointer-from-integer
  warnings on strict-aliasing or LP64 targets.

## Error handling conventions
- Init functions return `bool`: `true` on success, `false` on invalid parameters
  or hardware that cannot be configured (e.g. baud-rate divider overflow).
- Data-path functions (`write`, `read`, `transfer`) return `void` or `bool`
  depending on whether partial success is detectable.
- NULL pointer arguments to any public function are safe and produce no effect
  (or return `false`).

## Blocking operations
- All peripheral drivers are polling-based.  Blocking loops include a
  finite iteration timeout (see `I2C_POLL_TIMEOUT`, `HT5023_SPI_POLL_TIMEOUT`)
  to avoid infinite hangs on mis-configured or absent hardware.
- The buzzer driver busy-waits for the full note duration and must not be used
  with an RTOS without wrapping.

## ISR-safe ring buffer
- `ht5023_ringbuf_t` exposes `volatile` index and count fields so that changes
  made in an ISR are visible to the main context without compiler optimisation
  hiding them.
- Single-producer / single-consumer use (ISR writes, main reads) is safe without
  additional locking on Cortex-M0 provided that read-modify-write sequences on
  shared fields are not interrupted.  Use `push_overwrite` in RX ISRs to discard
  the oldest byte rather than blocking.

## I2C combined transfers
- `ht5023_i2c_write_read()` performs a write phase followed by a repeated START
  and a read phase without releasing the bus between them.  This is the standard
  pattern for register-addressed sensors and EEPROMs.

## Versioning
- The SDK version is in `sdk/include/ht5023/version.h`.
- Use `HT5023_SDK_AT_LEAST(major, minor, patch)` for compile-time version guards.

## Current migration status
- Core startup/system/linker are mirrored into `sdk/cores/ht5023`
- Board blinky GPIO support is mirrored into `sdk/boards/ht5023_blinky`
- Drivers are exported via wrapper headers in `sdk/include/ht5023`
- Legacy include paths remain supported for compatibility
