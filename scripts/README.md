# HT5023 — J-Link SWD Detection Toolkit

This kit helps you **detect/identify an HT5023** using **SEGGER J-Link** over **SWD**, without reading flash.

## Wiring (minimum)
- SWDIO → PB13
- SWCLK → PB15
- RESET → NRST (recommended for connect-under-reset)
- VTref → 3.3 V (target VCC)
- GND → GND
- Straps: **TEST = 0**, **JTAGWDTEN = 0**

## Quick start (Linux/macOS)
```bash
# Normal-speed detect (you can override SPEED, default 1000 kHz)
./run_detect.sh
# Slower + connect-under-reset (good if WDT races you)
SPEED=100 ./run_detect.sh
```

## Quick start (Windows)
```bat
run_detect.bat
:: or specify speed (kHz):
run_detect.bat 100
```

## What it does
The script resets & halts the core, then reads **CPUID** at `0xE000ED00`.
- If you see a 32-bit value (e.g., `0x410CC200`) printed, the SWD link is OK.
- Check the **PartNo** field: `(CPUID >> 4) & 0xFFF` → should be **0xC20** for **Cortex‑M0**.

## Extra: peek registers
```bash
./run_regs.sh
```

## Flash (J-Link, HT5023)
This repo also includes a J-Link flash helper that uses the HT5023 flash-controller sequence from the datasheet
(`WPREG` / `FLASHLOCK` / `FLASHCON`) and programs a `.bin` by 32-bit writes.

```bash
# From this folder; defaults to ../build/app.bin @ 0x00000000
./run_flash.sh

# Optional: slower SWD, keep target halted after flash
SPEED=100 RUN_AFTER_FLASH=0 ./run_flash.sh

# Optional: page-erase only touched pages instead of mass erase
ERASE_MODE=pages ./run_flash.sh

# Optional: custom binary + address
./run_flash.sh ../build/app.bin 0x00000000
```

Notes:
- Default `ERASE_MODE=mass` erases the whole 256 KiB code flash before programming.
- `DRY_RUN=1 ./run_flash.sh` generates the J-Link Commander script but does not invoke `JLinkExe`.

## Troubleshooting
- Ensure **VTref** is present (3.3 V) or J-Link won’t talk.
- Use **connect-under-reset**: keep RESET wired; try lower speed: `SPEED=100 ./run_detect.sh`.
- If the target firmware immediately enables a WDT, hold the board’s **RESET** low, start the script, then release RESET when J-Link connects.
- Double-check straps (**TEST=0**, **JTAGWDTEN=0**) so PB13/PB15 are SWD.
- SWDIO/SWCLK swapped is a classic gotcha; re-check continuity.
