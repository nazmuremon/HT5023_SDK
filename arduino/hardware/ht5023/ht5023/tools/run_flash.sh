#!/usr/bin/env bash
set -euo pipefail

: "${SPEED:=1000}"
: "${FLASH_ADDR:=0x00000000}"
: "${ERASE_MODE:=mass}"
: "${RUN_AFTER_FLASH:=1}"
: "${DRY_RUN:=0}"
: "${KEEP_JLINK_SCRIPT:=0}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BIN_PATH="${1:?binary path required}"
ADDR_ARG="${2:-$FLASH_ADDR}"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "Binary not found: $BIN_PATH" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH" >&2
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

FLASH_SIZE = 0x40000
PAGE_SIZE = 1024

CMU_BASE = 0x4000F000
WPREG = CMU_BASE + 0x00
FLASHCON = CMU_BASE + 0x34
FLASHLOCK = CMU_BASE + 0x38

FS_LOCKS = [
    (CMU_BASE + 0x60, 0x7161),
    (CMU_BASE + 0x64, 0x7262),
    (CMU_BASE + 0x68, 0x7363),
    (CMU_BASE + 0x6C, 0x7464),
    (CMU_BASE + 0x70, 0x7565),
    (CMU_BASE + 0x74, 0x7666),
    (CMU_BASE + 0x78, 0x7767),
    (CMU_BASE + 0x7C, 0x7868),
    (CMU_BASE + 0x80, 0x7969),
    (CMU_BASE + 0x84, 0x7A6A),
    (CMU_BASE + 0x88, 0x7B6B),
]

data = bin_path.read_bytes()
if not data:
    raise SystemExit("Input binary is empty")

raw_len = len(data)
if addr % 4:
    raise SystemExit(f"Flash address must be 4-byte aligned, got 0x{addr:X}")
if addr < 0 or addr >= FLASH_SIZE:
    raise SystemExit(f"Flash address 0x{addr:X} is outside code flash")
if addr + raw_len > FLASH_SIZE:
    raise SystemExit("Binary exceeds code flash size")

pad = (-raw_len) % 4
if pad:
    data += b"\xFF" * pad

last_byte_addr = addr + raw_len - 1
first_page = addr & ~(PAGE_SIZE - 1)
last_page = last_byte_addr & ~(PAGE_SIZE - 1)
pages = list(range(first_page, last_page + 1, PAGE_SIZE))

def emit(handle, line):
    handle.write(line + "\n")

with out_path.open("w", encoding="ascii", newline="\n") as handle:
    emit(handle, "r")
    emit(handle, "halt")
    emit(handle, f"w2 0x{WPREG:08X}, 0xA55A")

    if erase_mode == "mass":
      emit(handle, f"w2 0x{FLASHLOCK:08X}, 0x7A68")
      emit(handle, f"w1 0x{FLASHCON:08X}, 0x03")
      emit(handle, "w4 0x00000000, 0xFFFFFFFF")
      emit(handle, "sleep 20")
      emit(handle, f"w1 0x{FLASHCON:08X}, 0x00")
    else:
      emit(handle, f"w2 0x{FLASHLOCK:08X}, 0x7A68")
      emit(handle, f"w1 0x{FLASHCON:08X}, 0x02")
      for page in pages:
          emit(handle, f"w4 0x{page:08X}, 0xFFFFFFFF")
          emit(handle, "sleep 3")
      emit(handle, f"w1 0x{FLASHCON:08X}, 0x00")

    emit(handle, f"w2 0x{FLASHLOCK:08X}, 0x0000")
    for reg, key in FS_LOCKS:
        emit(handle, f"w2 0x{reg:08X}, 0x{key:04X}")

    emit(handle, f"w1 0x{FLASHCON:08X}, 0x01")
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i + 4], "little")
        emit(handle, f"w4 0x{addr + i:08X}, 0x{word:08X}")
    emit(handle, f"w1 0x{FLASHCON:08X}, 0x00")

    emit(handle, f"w2 0x{FLASHLOCK:08X}, 0x0000")
    emit(handle, f"w2 0x{WPREG:08X}, 0x0000")
    emit(handle, "mem32 0x00000000, 4")

    if run_after:
        emit(handle, "r")
        emit(handle, "g")

    emit(handle, "q")
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
