#!/usr/bin/env bash
# ============================================================
#  HT5023 One-Click Flasher  --  Linux / macOS launcher
#
#  Double-click it in a file manager, run it from a terminal, or let the
#  desktop entry installed by ./install-linux.sh start it from the app menu.
#
#  Location-independent, like its Windows twin: it finds the GUI script next
#  to itself or in a tools/ subfolder, and the app discovers the project root
#  from whatever folder you open in it.
# ============================================================
set -uo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Launched from a desktop entry there is no terminal to print to, so every
# failure has to arrive in a window the user can actually see.
die() {
    local msg="$1"
    if   command -v zenity   >/dev/null 2>&1; then
        zenity --error --no-wrap --title="HT5023 Flasher" --text="$msg"
    elif command -v kdialog  >/dev/null 2>&1; then
        kdialog --title "HT5023 Flasher" --error "$msg"
    elif command -v xmessage >/dev/null 2>&1; then
        xmessage -center "HT5023 Flasher

$msg"
    fi
    printf 'HT5023 Flasher: %s\n' "$msg" >&2
    exit 1
}

APP=""
for cand in "$HERE/ht5023_flasher.py" "$HERE/tools/ht5023_flasher.py"; do
    [ -f "$cand" ] && { APP="$cand"; break; }
done
[ -n "$APP" ] || die "Could not find ht5023_flasher.py next to this launcher.
Keep the launcher in the HT5023 project folder."

# Pick an interpreter that actually has tkinter: a distro can ship python3
# without it (Debian/Ubuntu split it into python3-tk), and the GUI is the
# whole point of this launcher.
PY=""
for cand in "${HT5023_PYTHON:-}" python3 python; do
    [ -n "$cand" ] || continue
    command -v "$cand" >/dev/null 2>&1 || continue
    if "$cand" -c 'import tkinter' >/dev/null 2>&1; then PY="$cand"; break; fi
    [ -n "${PY_NO_TK:-}" ] || PY_NO_TK="$cand"
done

if [ -z "$PY" ]; then
    if [ -n "${PY_NO_TK:-}" ]; then
        die "Python is installed but tkinter is missing, so the window cannot open.

Install it:
    Debian/Ubuntu:  sudo apt install python3-tk
    Fedora:         sudo dnf install python3-tkinter
    Arch:           sudo pacman -S tk

Or run  ./install-linux.sh  in the project folder to set up everything at once."
    fi
    die "Python 3 was not found.

Install it:
    Debian/Ubuntu:  sudo apt install python3 python3-tk python3-serial
    Fedora:         sudo dnf install python3 python3-tkinter python3-pyserial
    Arch:           sudo pacman -S python tk python-pyserial

Or run  ./install-linux.sh  in the project folder."
fi

# pyserial is only needed for the serial log and USB uploads; the app says so
# itself and still runs without it, so this is a note rather than a failure.
"$PY" -c 'import serial' >/dev/null 2>&1 || \
    printf 'HT5023 Flasher: pyserial is missing -- the serial log and USB uploads are unavailable (pip install pyserial).\n' >&2

cd "$HERE"
exec "$PY" "$APP" "$@"
