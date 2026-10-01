#!/usr/bin/env python3
"""Install the HT5023 core into the Arduino sketchbook.

    python arduino/install.py            # install / reinstall
    python arduino/install.py --where    # just print where it would go

After this, "HT5023 Board" shows up under Tools > Board in the Arduino IDE and
as ht5023:ht5023:ht5023 for arduino-cli.

The sketchbook is asked of arduino-cli when it is available, because Windows
users with OneDrive have Documents redirected and the obvious guess
(~/Documents/Arduino) is then the wrong folder -- the IDE looks in
~/OneDrive/Documents/Arduino and silently shows no board.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "hardware" / "ht5023" / "ht5023"
REPO = HERE.parent

# Shared with the Flasher and the command line; the Arduino uploader imports it
# from its own tools directory once installed.
EXTRA_TOOLS = ["ht5023_uart_flash.py", "ht5023_flash_seq.py"]

CLI_CANDIDATES = [
    r"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    r"C:\Program Files (x86)\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    "/usr/local/bin/arduino-cli",
    "/usr/bin/arduino-cli",
]


def find_cli():
    found = shutil.which("arduino-cli")
    if found:
        return found
    for c in CLI_CANDIDATES:
        if Path(c).is_file():
            return c
    local = Path.home() / "Applications" / "Arduino IDE" / "resources" / "app" \
        / "lib" / "backend" / "resources" / "arduino-cli"
    return str(local) if local.is_file() else None


def sketchbook():
    cli = find_cli()
    if cli:
        try:
            out = subprocess.run([cli, "config", "get", "directories.user"],
                                 capture_output=True, text=True, timeout=30)
            path = out.stdout.strip().strip('"')
            if path and Path(path).parent.exists():
                return Path(path)
        except Exception:
            pass

    home = Path.home()
    for guess in (home / "OneDrive" / "Documents" / "Arduino",
                  home / "Documents" / "Arduino",
                  home / "Arduino"):
        if guess.exists():
            return guess
    return home / "Documents" / "Arduino" if os.name == "nt" else home / "Arduino"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--where", action="store_true",
                    help="print the destination and exit")
    ap.add_argument("--dest", help="install somewhere else (a sketchbook root)")
    args = ap.parse_args()

    book = Path(args.dest) if args.dest else sketchbook()
    dest = book / "hardware" / "ht5023" / "ht5023"

    if args.where:
        print(dest)
        return 0

    if not SRC.is_dir():
        sys.exit(f"core not found at {SRC}")

    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(SRC, dest)

    # The uploader needs the bootloader client next to it once the core is
    # outside the SDK tree.
    for name in EXTRA_TOOLS:
        src = REPO / "tools" / name
        if src.is_file():
            shutil.copy2(src, dest / "tools" / name)

    print(f"installed: {dest}")
    cli = find_cli()
    if cli:
        # Port names are the one part of the command line that differs per OS:
        # COM11 on Windows, /dev/ttyUSB0 on Linux, /dev/cu.* on macOS.
        port = "COM11" if os.name == "nt" else (
            "/dev/cu.usbserial-0001" if sys.platform == "darwin" else "/dev/ttyUSB0")
        print(f"arduino-cli: {cli}")
        print("  compile:  arduino-cli compile -b ht5023:ht5023:ht5023 <sketch>")
        print("  upload:   arduino-cli upload  -b ht5023:ht5023:ht5023"
              f":layout=bootloader -p {port} <sketch>")
    print("Restart the Arduino IDE to pick up the board.")
    if os.name != "nt":
        print("If uploading fails with 'permission denied', join the serial group:")
        print("  sudo usermod -aG dialout $USER   (then log out and back in)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
