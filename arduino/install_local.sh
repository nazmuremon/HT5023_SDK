#!/usr/bin/env bash
# Kept for habit: the installer is now cross-platform Python, because the
# sketchbook is not always ~/Arduino (Windows with OneDrive redirects it) and
# the uploader needs the bootloader client copied in beside it.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$SCRIPT_DIR/install.py" "$@"
