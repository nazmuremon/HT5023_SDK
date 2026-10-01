# HT5023 SDK Layout

This `sdk/` folder provides a more standard embedded SDK structure on top of the existing project.

## Layers
- `sdk/cores/ht5023`: startup, system init, linker script, device/core headers
- `sdk/boards/ht5023_blinky`: board support for the demo board (LED pins, GPIO helpers)
- `sdk/include/ht5023`: stable public include namespace (`#include <ht5023/...>`)

Current driver implementations still live under `lib/ht5023_*` and are exposed through the public wrappers in `sdk/include/ht5023/`. This keeps backward compatibility while establishing a stable SDK API surface.
