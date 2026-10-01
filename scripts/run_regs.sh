#!/usr/bin/env bash
set -e
: "${SPEED:=1000}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v JLinkExe >/dev/null 2>&1; then
  echo "JLinkExe not found in PATH"; exit 1
fi
JLinkExe -device Cortex-M0 -if SWD -speed "$SPEED" -autoconnect 1 -CommanderScript "$SCRIPT_DIR/regs.jlink"
