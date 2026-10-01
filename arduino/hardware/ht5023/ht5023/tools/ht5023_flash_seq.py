#!/usr/bin/env python3
"""The HT5023 SWD flash sequence, as a J-Link Commander script.

J-Link has no flash algorithm for this part, so `loadbin` cannot program it --
it reports "Writing target memory failed" and stops. Flash has to be driven
through the chip's own registers instead: unlock, mass erase, unlock every
sector, write word by word, relock, read back.

This module is the single copy of that sequence. It is shared by the Flasher
GUI, the Arduino uploader and the PlatformIO builder so the three cannot drift
apart -- an earlier split had exactly that happen.

Standalone use:
    python3 ht5023_flash_seq.py firmware.bin              # program and verify
    python3 ht5023_flash_seq.py firmware.bin --script-only out.jlink
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEVICE = "Cortex-M0"
FLASH_ADDR = 0x00000000
FLASH_SIZE = 0x40000            # 256 KiB code flash

# Clock/flash control block (datasheet 2.3).
CMU = 0x4000F000
WPREG, FLASHCON, FLASHLOCK = CMU + 0x00, CMU + 0x34, CMU + 0x38

# Per-partition write-lock registers and their keys.
FS_LOCKS = [
    (CMU + 0x60, 0x7161), (CMU + 0x64, 0x7262), (CMU + 0x68, 0x7363),
    (CMU + 0x6C, 0x7464), (CMU + 0x70, 0x7565), (CMU + 0x74, 0x7666),
    (CMU + 0x78, 0x7767), (CMU + 0x7C, 0x7868), (CMU + 0x80, 0x7969),
    (CMU + 0x84, 0x7A6A), (CMU + 0x88, 0x7B6B),
]

# A non-0xFF byte at 0x0FC1 turns on read protection, which stops the part
# being read or debugged again until a mass erase. Nothing may be written here.
OPTION_BYTES = 0x0FC0


def verify_offsets(size, count=12):
    """Word-aligned offsets spread over the image, always including the last."""
    if size < 16:
        return [0]
    last = (size - 16) & ~3
    step = max(4, (last // max(1, count - 1)) & ~3)
    offs = list(range(0, last + 1, step))
    if offs[-1] != last:
        offs.append(last)
    return offs


def generate_flash_script(bin_path, addr, out_path):
    """Write the Commander script for `bin_path` at `addr`. Returns image size."""
    bin_path, out_path = Path(bin_path), Path(out_path)
    data = bin_path.read_bytes()
    if not data:
        raise ValueError("Built binary is empty.")
    if addr % 4:
        raise ValueError("Flash address must be 4-byte aligned.")
    if addr + len(data) > FLASH_SIZE:
        raise ValueError("Binary exceeds 256 KiB code flash.")

    if addr <= OPTION_BYTES < addr + len(data):
        off = OPTION_BYTES - addr
        if data[off:off + 4] != b"\xFF\xFF\xFF\xFF":
            raise ValueError(
                "Image would write 0x%s over the flash option bytes at 0x0FC0. "
                "A non-0xFF value at 0x0FC1 enables read protection and stops "
                "the part being read or debugged until a mass erase. Fix the "
                "linker script so nothing is placed there."
                % data[off:off + 4].hex().upper())

    data += b"\xFF" * ((-len(data)) % 4)

    # Mass erase (FLASHCON=0x03, datasheet 2.3.2: 10 ms max). FLASHLOCK=0x7A68
    # alone grants erase rights over the whole array; the FSnLOCK partition keys
    # below are for the programming phase and need FLASHLOCK to be non-0x7A68.
    lines = ["r", "halt",
             f"w2 0x{WPREG:08X}, 0xA55A", f"w2 0x{FLASHLOCK:08X}, 0x7A68",
             f"w1 0x{FLASHCON:08X}, 0x03", f"w4 0x{addr:08X}, 0xFFFFFFFF",
             "sleep 100", f"w1 0x{FLASHCON:08X}, 0x00",
             f"w2 0x{FLASHLOCK:08X}, 0x0000"]
    lines += [f"w2 0x{reg:08X}, 0x{key:04X}" for reg, key in FS_LOCKS]
    lines.append(f"w1 0x{FLASHCON:08X}, 0x01")

    # Words already at the erased value need no write, which skips the padding
    # between the vector table and .text -- including the option-byte word.
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i + 4], "little")
        if word == 0xFFFFFFFF:
            continue
        lines.append(f"w4 0x{addr + i:08X}, 0x{word:08X}")

    lines += [f"w1 0x{FLASHCON:08X}, 0x00", f"w2 0x{FLASHLOCK:08X}, 0x0000",
              f"w2 0x{WPREG:08X}, 0x0000"]

    # Read-back checkpoints spread across the image, so a partial program
    # cannot pass as success.
    for off in verify_offsets(len(data)):
        lines.append(f"mem32 0x{addr + off:08X}, 4")
    lines += ["r", "g", "q"]

    out_path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    return len(data)


def find_jlink():
    """The J-Link Commander executable, or None."""
    exe = "JLink.exe" if os.name == "nt" else "JLinkExe"
    env = os.environ.get("JLINK_PATH")
    if env and Path(env).is_file():
        return env
    for root in (r"C:\Program Files\SEGGER", r"C:\Program Files (x86)\SEGGER",
                 "/opt/SEGGER", "/usr/lib", "/usr/local/lib"):
        base = Path(root)
        if base.is_dir():
            hits = sorted(base.glob(f"JLink*/{exe}")) + sorted(base.glob(exe))
            if hits:
                return str(hits[-1])
    found = shutil.which(exe)
    return found if found else None


def check_readback(text, image, addr=FLASH_ADDR):
    """Compare J-Link's mem32 dumps against the image. (checked, bad, first)"""
    readbacks = {}
    for m in re.finditer(r"^([0-9A-Fa-f]{8}) = ((?:[0-9A-Fa-f]{8} ?)+)", text, re.M):
        readbacks[int(m.group(1), 16)] = m.group(2).split()
    checked = bad = 0
    first = None
    for address, words in readbacks.items():
        off = address - addr
        if off < 0 or off + 4 * len(words) > len(image):
            continue
        for i, w in enumerate(words):
            expect = int.from_bytes(image[off + 4 * i:off + 4 * i + 4], "little")
            checked += 1
            if int(w, 16) != expect:
                bad += 1
                if first is None:
                    first = (address + 4 * i, int(w, 16), expect)
    return checked, bad, first


def program(bin_path, addr=FLASH_ADDR, speed=1000, log=print):
    """Program and verify over SWD. Returns a process-style exit code."""
    jlink = find_jlink()
    if jlink is None:
        log("J-Link software not found -- install it or set JLINK_PATH")
        return 1

    script = Path(tempfile.gettempdir()) / "ht5023_flash_seq.jlink"
    try:
        size = generate_flash_script(bin_path, addr, script)
    except (ValueError, OSError) as e:
        log(f"flash-script generation failed: {e}")
        return 1
    log(f"programming {size} bytes at 0x{addr:08X} over SWD via {jlink}")

    proc = subprocess.run(
        [jlink, "-device", DEVICE, "-if", "SWD", "-speed", str(speed),
         "-autoconnect", "1", "-CommanderScript", str(script)],
        capture_output=True, text=True, errors="replace")
    out = (proc.stdout or "") + (proc.stderr or "")

    if "Cannot connect" in out or "Could not connect" in out:
        log(out.strip())
        log("FAILED: J-Link could not connect -- is the probe plugged in and "
            "the target powered (VTref)?")
        return 1
    if "Script processing completed." not in out:
        log(out.strip())
        log("FAILED: J-Link did not finish the script.")
        return 1

    image = Path(bin_path).read_bytes()
    image += b"\xFF" * ((-len(image)) % 4)
    checked, bad, first = check_readback(out, image, addr)
    if bad:
        a, got, want = first
        log(f"VERIFY FAILED at 0x{a:08X}: flash has 0x{got:08X}, "
            f"image has 0x{want:08X}")
        log(f"FAILED: {bad} of {checked} verified words differ.")
        return 1
    if checked == 0:
        log(out.strip())
        log("FAILED: no readback to verify against.")
        return 1

    log(f"OK -- {checked} words verified, target reset and running.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary")
    ap.add_argument("--addr", default="0x0", help="flash address (default 0x0)")
    ap.add_argument("--speed", type=int, default=1000, help="SWD kHz")
    ap.add_argument("--script-only", metavar="PATH",
                    help="write the Commander script and exit")
    args = ap.parse_args()

    addr = int(args.addr, 0)
    if args.script_only:
        n = generate_flash_script(args.binary, addr, args.script_only)
        print(f"wrote {args.script_only} for {args.binary} ({n} bytes)")
        return 0
    return program(args.binary, addr, args.speed)


if __name__ == "__main__":
    sys.exit(main())
