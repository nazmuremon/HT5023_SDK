#!/usr/bin/env python3
"""Upload a compiled sketch to an HT5023, from the Arduino IDE or by hand.

Two ways in, matching the board's "Flash layout" menu:

  --method jlink   SWD through a SEGGER J-Link. Programs from 0x0000, so the
                   sketch owns the whole part. Pick the "Standalone" layout.
  --method usb     A USB-UART adapter on PC11/PC12, through the resident
                   bootloader. Programs from 0x2000. Pick the "UART bootloader"
                   layout, and reset the board when asked.

    python ht5023_upload.py --method usb --port COM11 --bin sketch.bin
    python ht5023_upload.py --method jlink --bin sketch.bin

The UART protocol lives in ht5023_uart_flash.py; this looks for it next to
itself first (the installer drops a copy in) and then in an SDK checkout.
"""

import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLASH_BASE = 0x00000000


def load_sdk_module(name):
    """Import an SDK tool module, wherever it ended up.

    The installer drops copies next to this file; an SDK checkout has them in
    <repo>/tools. Either is fine.
    """
    candidates = [HERE]
    # An SDK checkout: arduino/hardware/ht5023/ht5023/tools -> <repo>/tools
    candidates.append(HERE.parents[4] / "tools")
    env = os.environ.get("HT5023_SDK")
    if env:
        candidates.append(Path(env) / "tools")

    for d in candidates:
        if (d / (name + ".py")).is_file():
            sys.path.insert(0, str(d))
            return __import__(name)
    sys.exit(
        f"{name}.py not found -- reinstall the core with arduino/install.py, "
        "or set HT5023_SDK to your SDK checkout"
    )


def load_uart_flasher():
    """The bootloader client."""
    return load_sdk_module("ht5023_uart_flash")


def upload_jlink(binary, speed):
    """Mass erase, program from 0x0000, verify, run.

    Not `loadbin`: J-Link ships no flash algorithm for this part, so its
    generic loader fails with "Writing target memory failed" and leaves the
    chip untouched. Flash has to be driven through the chip's own unlock /
    erase / program registers, which is what ht5023_flash_seq does -- the same
    sequence the Flasher GUI uses, kept in one place so the two cannot drift.
    """
    seq = load_sdk_module("ht5023_flash_seq")
    return seq.program(binary, addr=FLASH_BASE, speed=int(speed))


def upload_usb(binary, port, baud, addr, do_reset):
    flasher = load_uart_flasher()
    data = Path(binary).read_bytes()
    ser = flasher.open_port(port, baud)
    try:
        banner = flasher.enter_bootloader(ser, do_reset=do_reset)
        if not banner:
            print("could not reach the bootloader -- is it installed, is the "
                  "adapter on PC11/PC12, and did the board reset?",
                  file=sys.stderr)
            return 1
        print(banner)
        if not flasher.program(ser, data, addr):
            return 1
        flasher.jump(ser)
        return 0
    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", required=True, help="compiled sketch (.bin)")
    ap.add_argument("--method", choices=("jlink", "usb"), default="jlink")
    ap.add_argument("--port", help="serial port for --method usb")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--speed", type=int, default=1000, help="SWD kHz")
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x2000,
                    help="load address for --method usb")
    ap.add_argument("--reset", action="store_true",
                    help="pulse DTR/RTS to reset, if wired")
    args = ap.parse_args()

    binary = Path(args.bin)
    if not binary.is_file():
        sys.exit(f"not found: {binary}")

    if args.method == "usb":
        if not args.port:
            sys.exit("--port is required for --method usb")
        return upload_usb(binary, args.port, args.baud, args.addr, args.reset)
    return upload_jlink(binary, args.speed)


if __name__ == "__main__":
    sys.exit(main())
