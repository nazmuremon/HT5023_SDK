# HT5023 PlatformIO Platform

This folder contains a local custom PlatformIO platform for the bundled HT5023 Arduino core.

Use it from another project with:

```ini
[env:ht5023]
platform = ../HT5023/platformio
board = ht5023
framework = arduino
```

Notes:

- The builder reuses the Arduino core under `arduino/hardware/ht5023/ht5023`.
- Upload uses the bundled J-Link flash helper.
- Framework libraries such as `SPI`, `Wire`, and `HT5023SDK` are exposed through the platform.
