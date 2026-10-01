#!/usr/bin/env bash
set -euo pipefail

: "${SPEED:=1000}"
: "${FLASH_ADDR:=0x00000000}"
: "${ERASE_MODE:=mass}"        # mass | pages
: "${RUN_AFTER_FLASH:=1}"      # 1 = reset+run after program
: "${DRY_RUN:=0}"              # 1 = generate script only
: "${KEEP_JLINK_SCRIPT:=0}"    # 1 = keep temp .jlink file

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_BIN="$SCRIPT_DIR/../build/app.bin"
BIN_PATH="${1:-${BIN_PATH:-$DEFAULT_BIN}}"
ADDR_ARG="${2:-$FLASH_ADDR}"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "Binary not found: $BIN_PATH" >&2
  echo "Build first (for this repo: run 'make' in the repo root) or pass a .bin path." >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH (needed to generate J-Link commands from the .bin)" >&2
  exit 1
fi

case "$ERASE_MODE" in
  mass|pages) ;;
  *)
    echo "Invalid ERASE_MODE='$ERASE_MODE' (use 'mass' or 'pages')" >&2
    exit 1
    ;;
esac

cmd_file="$(mktemp "${TMPDIR:-/tmp}/ht5023_flash.XXXXXX.jlink")"
cleanup() {
  if [[ "$KEEP_JLINK_SCRIPT" != "1" && -f "$cmd_file" ]]; then
    rm -f "$cmd_file"
  fi
}
trap cleanup EXIT

python3 - "$BIN_PATH" "$ADDR_ARG" "$cmd_file" "$ERASE_MODE" "$RUN_AFTER_FLASH" <<'PY'
import pathlib
import sys

bin_path = pathlib.Path(sys.argv[1])
addr = int(sys.argv[2], 0)
out_path = pathlib.Path(sys.argv[3])
erase_mode = sys.argv[4]
run_after = sys.argv[5] == "1"

FLASH_SIZE = 0x40000  # 256 KiB code flash
PAGE_SIZE = 1024

CMU_BASE = 0x4000F000
WPREG = CMU_BASE + 0x00
FLASHCON = CMU_BASE + 0x34
FLASHLOCK = CMU_BASE + 0x38

FS_LOCKS = [
    (CMU_BASE + 0x60, 0x7161),  # FS1  0x00000-0x01FFF
    (CMU_BASE + 0x64, 0x7262),  # FS2  0x02000-0x07FFF
    (CMU_BASE + 0x68, 0x7363),  # FS3  0x08000-0x0FFFF
    (CMU_BASE + 0x6C, 0x7464),  # FS4  0x10000-0x17FFF
    (CMU_BASE + 0x70, 0x7565),  # FS5  0x18000-0x1FBFF
    (CMU_BASE + 0x74, 0x7666),  # FS6  0x20000-0x27FFF
    (CMU_BASE + 0x78, 0x7767),  # FS7  0x28000-0x2FFFF
    (CMU_BASE + 0x7C, 0x7868),  # FS8  0x30000-0x37FFF
    (CMU_BASE + 0x80, 0x7969),  # FS9  0x38000-0x3FBFF
    (CMU_BASE + 0x84, 0x7A6A),  # FSA  0x1FC00-0x1FFFF
    (CMU_BASE + 0x88, 0x7B6B),  # FSB  0x3FC00-0x3FFFF
]

data = bin_path.read_bytes()
if not data:
    raise SystemExit("Input binary is empty")
raw_len = len(data)
if addr % 4:
    raise SystemExit(f"Flash address must be 4-byte aligned, got 0x{addr:X}")
if addr < 0 or addr >= FLASH_SIZE:
    raise SystemExit(f"Flash address 0x{addr:X} is outside code flash (0x00000000..0x{FLASH_SIZE-1:08X})")
if addr + raw_len > FLASH_SIZE:
    raise SystemExit(
        f"Binary (len={raw_len}) at 0x{addr:X} exceeds code flash size 0x{FLASH_SIZE:X}"
    )

pad = (-raw_len) % 4
if pad:
    data += b"\xFF" * pad

last_byte_addr = addr + raw_len - 1
first_page = addr & ~(PAGE_SIZE - 1)
last_page = last_byte_addr & ~(PAGE_SIZE - 1)
pages = list(range(first_page, last_page + 1, PAGE_SIZE))

def emit(f, line: str) -> None:
    f.write(line + "\n")

with out_path.open("w", encoding="ascii", newline="\n") as f:
    emit(f, "r")
    emit(f, "halt")

    # Datasheet: write-protect unlock + flash control path.
    emit(f, f"w2 0x{WPREG:08X}, 0xA55A")

    if erase_mode == "mass":
        emit(f, f"w2 0x{FLASHLOCK:08X}, 0x7A68")
        emit(f, f"w1 0x{FLASHCON:08X}, 0x03")
        emit(f, "w4 0x00000000, 0xFFFFFFFF")
        emit(f, "sleep 20")
        emit(f, f"w1 0x{FLASHCON:08X}, 0x00")
    else:
        emit(f, f"w2 0x{FLASHLOCK:08X}, 0x7A68")
        emit(f, f"w1 0x{FLASHCON:08X}, 0x02")
        for page in pages:
            emit(f, f"w4 0x{page:08X}, 0xFFFFFFFF")
            emit(f, "sleep 3")
        emit(f, f"w1 0x{FLASHCON:08X}, 0x00")

    # Datasheet text is ambiguous; unlock all sector write locks before program.
    emit(f, f"w2 0x{FLASHLOCK:08X}, 0x0000")
    for reg, key in FS_LOCKS:
        emit(f, f"w2 0x{reg:08X}, 0x{key:04X}")

    emit(f, f"w1 0x{FLASHCON:08X}, 0x01")
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i+4], "little")
        emit(f, f"w4 0x{addr + i:08X}, 0x{word:08X}")
    emit(f, f"w1 0x{FLASHCON:08X}, 0x00")

    # Relock protected registers / flash path.
    emit(f, f"w2 0x{FLASHLOCK:08X}, 0x0000")
    emit(f, f"w2 0x{WPREG:08X}, 0x0000")

    # Small post-write readback (full verify is left to a separate pass).
    emit(f, "mem32 0x00000000, 4")

    if run_after:
        emit(f, "r")
        emit(f, "g")

    emit(f, "q")

print(
    f"Generated {out_path} for {bin_path} "
    f"({raw_len} bytes, padded {pad}, {len(pages)} page(s), erase={erase_mode})"
)
PY

if [[ "$DRY_RUN" == "1" ]]; then
  echo "DRY_RUN=1, not invoking JLinkExe."
  echo "Commander script: $cmd_file"
  exit 0
fi

if ! command -v JLinkExe >/dev/null 2>&1; then
  echo "JLinkExe not found in PATH" >&2
  exit 1
fi

JLinkExe -device Cortex-M0 -if SWD -speed "$SPEED" -autoconnect 1 -CommanderScript "$cmd_file"
