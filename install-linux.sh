#!/usr/bin/env bash
# ============================================================
#  HT5023 SDK -- one-time Linux setup.
#
#      ./install-linux.sh            # ask before each step
#      ./install-linux.sh --yes      # assume yes (for scripted installs)
#      ./install-linux.sh --no-packages   # skip anything needing sudo
#
#  Installs the build toolchain, the Python bits the GUI needs, a desktop
#  entry so "HT5023 Flasher" appears in the application menu, and puts you in
#  the group that is allowed to open serial adapters.
#
#  Everything here is idempotent -- running it twice is harmless.
# ============================================================
set -uo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSUME_YES=0
DO_PACKAGES=1

for arg in "$@"; do
    case "$arg" in
        -y|--yes)      ASSUME_YES=1 ;;
        --no-packages) DO_PACKAGES=0 ;;
        -h|--help)     sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

bold() { printf '\033[1m%s\033[0m\n' "$*"; }
info() { printf '  %s\n' "$*"; }
warn() { printf '\033[33m  ! %s\033[0m\n' "$*"; }
ok()   { printf '\033[32m  * %s\033[0m\n' "$*"; }

ask() {
    [ "$ASSUME_YES" = 1 ] && return 0
    local reply
    read -r -p "  $1 [Y/n] " reply </dev/tty || return 1
    [[ -z "$reply" || "$reply" =~ ^[Yy] ]]
}

FAILED=()

# ---------------------------------------------------------------- packages
# Package names differ per distro; the tools they provide do not.
detect_pm() {
    for pm in apt-get dnf pacman zypper apk; do
        command -v "$pm" >/dev/null 2>&1 && { echo "$pm"; return; }
    done
    echo ""
}

install_packages() {
    local pm; pm="$(detect_pm)"
    local -a pkgs
    case "$pm" in
        apt-get) pkgs=(gcc-arm-none-eabi binutils-arm-none-eabi make python3 python3-tk python3-serial) ;;
        dnf)     pkgs=(arm-none-eabi-gcc-cs arm-none-eabi-binutils make python3 python3-tkinter python3-pyserial) ;;
        pacman)  pkgs=(arm-none-eabi-gcc arm-none-eabi-binutils make python tk python-pyserial) ;;
        zypper)  pkgs=(cross-arm-none-gcc14 cross-arm-none-binutils make python3 python3-tk python3-pyserial) ;;
        apk)     pkgs=(gcc-arm-none-eabi newlib-arm-none-eabi make python3 python3-tkinter py3-pyserial) ;;
        *)
            warn "No supported package manager found (apt/dnf/pacman/zypper/apk)."
            warn "Install by hand: arm-none-eabi-gcc, arm-none-eabi-binutils, make,"
            warn "python3, python3-tk (tkinter) and pyserial."
            FAILED+=("packages")
            return 1 ;;
    esac

    info "Package manager: $pm"
    info "Will install: ${pkgs[*]}"
    ask "Install them now (needs sudo)?" || { warn "skipped"; return 1; }

    case "$pm" in
        apt-get) sudo apt-get update && sudo apt-get install -y "${pkgs[@]}" ;;
        dnf)     sudo dnf install -y "${pkgs[@]}" ;;
        pacman)  sudo pacman -S --needed --noconfirm "${pkgs[@]}" ;;
        zypper)  sudo zypper install -y "${pkgs[@]}" ;;
        apk)     sudo apk add "${pkgs[@]}" ;;
    esac
}

# ---------------------------------------------------------------- checks
have_tool() { command -v "$1" >/dev/null 2>&1; }

report_tools() {
    local missing=0
    for t in arm-none-eabi-gcc arm-none-eabi-objcopy make; do
        if have_tool "$t"; then ok "$t  ->  $(command -v "$t")"
        else warn "$t is missing"; missing=1; fi
    done
    if python3 -c 'import tkinter' >/dev/null 2>&1; then ok "python3 tkinter"
    else warn "python3 tkinter is missing -- the GUI cannot open"; missing=1; fi
    if python3 -c 'import serial' >/dev/null 2>&1; then ok "python3 pyserial"
    else warn "pyserial is missing -- the serial log and USB uploads are unavailable"; fi
    if have_tool arduino-cli; then ok "arduino-cli  ->  $(command -v arduino-cli)"
    elif [ -x "$HOME/.local/bin/arduino-cli" ]; then ok "arduino-cli  ->  $HOME/.local/bin/arduino-cli"
    else info "arduino-cli not installed (optional; only .ino sketches need it)."; fi
    if have_tool JLinkExe; then ok "JLinkExe  ->  $(command -v JLinkExe)"
    else
        info "JLinkExe not installed (optional). SWD programming and semihosting"
        info "need it: https://www.segger.com/downloads/jlink/  -- uploading over"
        info "'USB (bootloader)' works without it."
    fi
    return $missing
}

# ---------------------------------------------------------------- J-Link
# SEGGER does not publish a repository, so the package is fetched straight from
# their download page. That page gates the file behind a licence checkbox; the
# POST field below is that checkbox, so running this step means accepting
# SEGGER's licence terms on your own behalf.
JLINK_URL_BASE="https://www.segger.com/downloads/jlink"

jlink_package() {
    # <filename> <installer-kind> for this machine, or nothing if unsupported.
    local arch kind ext
    arch="$(uname -m)"
    case "$arch" in
        x86_64|amd64)  arch=x86_64 ;;
        aarch64|arm64) arch=arm64 ;;
        armv7l|armhf)  arch=arm ;;
        i?86)          arch=i386 ;;
        *) return 1 ;;
    esac
    case "$(detect_pm)" in
        apt-get)      kind=deb; ext=deb ;;
        dnf|zypper)   kind=rpm; ext=rpm ;;
        *)            kind=tgz; ext=tgz ;;
    esac
    printf 'JLink_Linux_%s.%s %s\n' "$arch" "$ext" "$kind"
}

install_jlink() {
    if have_tool JLinkExe; then
        ok "JLinkExe is already installed  ->  $(command -v JLinkExe)"
        return 0
    fi

    info "The J-Link software is only needed for SWD programming and"
    info "semihosting -- uploading over 'USB (bootloader)' works without it."
    ask "Download and install it from segger.com (needs sudo)?" || {
        warn "skipped -- install it later from $JLINK_URL_BASE"
        return 1
    }

    local spec file kind
    spec="$(jlink_package)" || {
        warn "No J-Link build for $(uname -m); download it by hand from $JLINK_URL_BASE"
        FAILED+=("jlink")
        return 1
    }
    file="${spec%% *}"; kind="${spec##* }"

    local dir="${TMPDIR:-/tmp}/ht5023-jlink"
    mkdir -p "$dir"
    if [ -s "$dir/$file" ]; then
        info "Using the copy already downloaded: $dir/$file"
    else
        info "Downloading $file (about 60 MB)..."
        # accept_license_agreement is the licence checkbox on SEGGER's page.
        if ! curl -fL --progress-bar \
                -d 'accept_license_agreement=accepted&non_emb_ctr=confirmed' \
                -o "$dir/$file" "$JLINK_URL_BASE/$file"; then
            warn "Download failed. Get $file by hand from $JLINK_URL_BASE"
            FAILED+=("jlink")
            return 1
        fi
    fi

    case "$kind" in
        deb) sudo apt-get install -y "$dir/$file" ;;
        rpm) if command -v dnf >/dev/null 2>&1; then sudo dnf install -y "$dir/$file"
             else sudo zypper --non-interactive install "$dir/$file"; fi ;;
        tgz) sudo mkdir -p /opt/SEGGER \
             && sudo tar -xzf "$dir/$file" -C /opt/SEGGER \
             && sudo ln -sf /opt/SEGGER/JLink*/JLinkExe /usr/local/bin/JLinkExe \
             && info "Copy 99-jlink.rules from /opt/SEGGER/JLink*/ into /etc/udev/rules.d/" ;;
    esac

    if have_tool JLinkExe; then
        ok "Installed  ->  $(command -v JLinkExe)"
        ok "$(JLinkExe -nogui 1 </dev/null 2>&1 | head -1)"
        info "Unplug and replug the probe so the new udev rules apply."
    else
        warn "The installer ran but JLinkExe is still not on PATH."
        FAILED+=("jlink")
        return 1
    fi
}

# ---------------------------------------------------------------- arduino-cli
# Needed only to build .ino sketches. Installed under ~/.local/bin so this step
# never needs sudo; the Flasher looks there explicitly, so it is found even if
# that directory is not on PATH.
ARDUINO_CLI_DIR="$HOME/.local/bin"

arduino_cli_asset() {
    case "$(uname -m)" in
        x86_64|amd64)  echo Linux_64bit ;;
        i?86)          echo Linux_32bit ;;
        aarch64|arm64) echo Linux_ARM64 ;;
        armv7l|armhf)  echo Linux_ARMv7 ;;
        armv6l)        echo Linux_ARMv6 ;;
        *) return 1 ;;
    esac
}

install_arduino_cli() {
    if have_tool arduino-cli; then
        ok "arduino-cli is already installed  ->  $(command -v arduino-cli)"
    elif [ -x "$ARDUINO_CLI_DIR/arduino-cli" ]; then
        ok "arduino-cli is already installed  ->  $ARDUINO_CLI_DIR/arduino-cli"
    else
        info "arduino-cli builds the .ino sketches in sketches/. The Makefile"
        info "examples in examples/ do not need it."
        ask "Download and install arduino-cli into $ARDUINO_CLI_DIR (no sudo)?" || {
            warn "skipped"
            return 1
        }

        local asset ver url tmp
        asset="$(arduino_cli_asset)" || {
            warn "No arduino-cli build for $(uname -m); install it by hand."
            FAILED+=("arduino-cli"); return 1
        }
        ver="$(curl -fsSL https://api.github.com/repos/arduino/arduino-cli/releases/latest \
               | sed -n 's/.*"tag_name": *"v\([^"]*\)".*/\1/p' | head -1)"
        [ -n "$ver" ] || { warn "Could not read the latest arduino-cli version."
                           FAILED+=("arduino-cli"); return 1; }

        tmp="$(mktemp -d)"
        url="https://github.com/arduino/arduino-cli/releases/download/v$ver/arduino-cli_${ver}_${asset}.tar.gz"
        info "Downloading arduino-cli $ver ($asset)..."
        if ! curl -fL --progress-bar -o "$tmp/cli.tar.gz" "$url"; then
            warn "Download failed: $url"
            rm -rf "$tmp"; FAILED+=("arduino-cli"); return 1
        fi

        # Verify against the checksum file published with the release.
        local want got
        want="$(curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v$ver/${ver}-checksums.txt" \
                2>/dev/null | grep "_${asset}.tar.gz" | awk '{print $1}' | head -1)"
        got="$(sha256sum "$tmp/cli.tar.gz" | awk '{print $1}')"
        if [ -n "$want" ] && [ "$want" != "$got" ]; then
            warn "Checksum mismatch -- refusing to install."
            warn "  expected $want"
            warn "  got      $got"
            rm -rf "$tmp"; FAILED+=("arduino-cli"); return 1
        fi
        [ -n "$want" ] && ok "checksum verified" || warn "no checksum published; skipped verification"

        mkdir -p "$ARDUINO_CLI_DIR"
        tar xzf "$tmp/cli.tar.gz" -C "$ARDUINO_CLI_DIR" arduino-cli
        chmod +x "$ARDUINO_CLI_DIR/arduino-cli"
        rm -rf "$tmp"
        ok "Installed  ->  $ARDUINO_CLI_DIR/arduino-cli ($("$ARDUINO_CLI_DIR/arduino-cli" version 2>/dev/null | head -1))"
        case ":$PATH:" in
            *":$ARDUINO_CLI_DIR:"*) ;;
            *) info "$ARDUINO_CLI_DIR is not on PATH -- the Flasher still finds it," 
               info "but add it to your shell profile to use arduino-cli directly." ;;
        esac
    fi

    # The HT5023 core has to be in the sketchbook before any sketch will build.
    if [ -f "$HERE/arduino/install.py" ]; then
        info "Installing the HT5023 board core into the Arduino sketchbook..."
        if PATH="$ARDUINO_CLI_DIR:$PATH" python3 "$HERE/arduino/install.py" >/dev/null 2>&1; then
            ok "HT5023 core installed (board: ht5023:ht5023:ht5023)"
        else
            warn "Could not install the core -- run: python3 arduino/install.py"
            FAILED+=("arduino core")
        fi
    fi
}

# ---------------------------------------------------------------- serial group
serial_group() {
    for g in dialout uucp plugdev; do
        getent group "$g" >/dev/null 2>&1 && { echo "$g"; return; }
    done
    echo dialout
}

setup_serial_group() {
    local group; group="$(serial_group)"
    if id -nG "$USER" | tr ' ' '\n' | grep -qx "$group"; then
        ok "$USER is already in the '$group' group"
        return 0
    fi
    info "Serial adapters (/dev/ttyUSB*, /dev/ttyACM*) belong to the '$group' group."
    info "Without it, opening the port fails with 'permission denied'."
    ask "Add $USER to '$group' (needs sudo)?" || { warn "skipped"; return 1; }
    if sudo usermod -aG "$group" "$USER"; then
        ok "Added. Log out and back in for it to take effect (a reboot also works)."
    else
        warn "usermod failed"
        FAILED+=("serial group")
    fi
}

# ---------------------------------------------------------------- desktop entry
DESKTOP_ID="ht5023-flasher.desktop"
LAUNCHER="$HERE/HT5023 Flasher.sh"
ICON_PNG="$HERE/tools/assets/ht5023-flasher.png"
ICON_SVG="$HERE/tools/assets/ht5023-flasher.svg"

write_desktop_file() {
    local target="$1" icon="$ICON_PNG"
    [ -f "$ICON_SVG" ] && icon="$ICON_SVG"
    [ -f "$icon" ] || icon="applications-electronics"
    # Exec/Icon must be absolute: a desktop entry is run from an unrelated
    # working directory. Quotes cover the space in the launcher's name.
    cat > "$target" <<EOF
[Desktop Entry]
Type=Application
Version=1.1
Name=HT5023 Flasher
GenericName=Microcontroller IDE
Comment=Edit, compile and upload HT5023 firmware over SWD or USB
Exec="$LAUNCHER"
Icon=$icon
Path=$HERE
Terminal=false
Categories=Development;IDE;Electronics;
Keywords=HT5023;embedded;flash;firmware;arduino;serial;
StartupNotify=true
EOF
    chmod +x "$target"
}

install_desktop_entry() {
    local apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    mkdir -p "$apps"
    write_desktop_file "$apps/$DESKTOP_ID"
    ok "Application menu entry: $apps/$DESKTOP_ID"

    # A copy in the project folder so double-clicking works there too. GNOME
    # Files needs it marked trusted before it will run one.
    write_desktop_file "$HERE/$DESKTOP_ID"
    if command -v gio >/dev/null 2>&1; then
        gio set "$HERE/$DESKTOP_ID" metadata::trusted true 2>/dev/null || true
    fi
    ok "Double-clickable copy: $HERE/$DESKTOP_ID"

    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database "$apps" 2>/dev/null || true
}

# ---------------------------------------------------------------- go
bold "HT5023 SDK -- Linux setup"
echo "  Project: $HERE"
echo

if [ "$DO_PACKAGES" = 1 ]; then
    bold "1. Build tools and Python"
    install_packages || true
else
    bold "1. Build tools and Python  (skipped: --no-packages)"
fi
echo

bold "2. What is available now"
report_tools || FAILED+=("required tools")
echo

bold "3. SEGGER J-Link (optional, for SWD)"
if [ "$DO_PACKAGES" = 1 ]; then
    install_jlink || true
else
    info "skipped: --no-packages"
fi
echo

bold "4. Arduino sketch support (optional)"
install_arduino_cli || true
echo

bold "5. Serial port access"
setup_serial_group || true
echo

bold "6. One-click launcher"
chmod +x "$LAUNCHER" "$HERE/install-linux.sh" 2>/dev/null || true
chmod +x "$HERE"/scripts/*.sh "$HERE/JLinkExe" 2>/dev/null || true
if [ -f "$LAUNCHER" ]; then
    install_desktop_entry
else
    warn "'HT5023 Flasher.sh' is missing from $HERE"
    FAILED+=("launcher")
fi
echo

bold "7. Test build"
if have_tool arm-none-eabi-gcc && have_tool make; then
    if (cd "$HERE" && python3 tools/ht5023_flasher.py --selftest blinky >/dev/null 2>&1); then
        ok "'blinky' compiled -- the toolchain works."
    else
        warn "The test build failed. Run this to see why:"
        warn "    cd '$HERE' && python3 tools/ht5023_flasher.py --selftest blinky"
        FAILED+=("test build")
    fi
else
    warn "Skipped -- the toolchain is not installed yet."
fi
echo

if [ ${#FAILED[@]} -eq 0 ]; then
    bold "Done."
    echo "  Start it from the application menu ('HT5023 Flasher'), by double-clicking"
    echo "  'HT5023 Flasher.sh' in the project folder, or from a terminal:"
    echo "      ./'HT5023 Flasher.sh'"
else
    bold "Finished with warnings: ${FAILED[*]}"
    echo "  Re-run this script after fixing them, or see README.md."
fi
