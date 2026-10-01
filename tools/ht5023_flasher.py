#!/usr/bin/env python3
"""HT5023 One-Click Flasher.

A self-contained VSCode-style editor with an Explorer sidebar and a single
"Compile & Upload" button that:
  1. saves the open source file,
  2. builds it -- examples/<name>/main.c with arm-none-eabi-gcc + make, or an
     Arduino sketch (.ino) with arduino-cli,
  3. programs it, either over SWD through a SEGGER J-Link (a generated J-Link
     Commander script running the HT5023 flash sequence) or over USB-UART
     through the resident bootloader,
  4. reopens the debug log so the target's output lands in the same window.

The app is location-independent: it discovers the HT5023 project root (the
folder containing the Makefile) from whatever file/folder you open, so it can
live anywhere.

Runs on Windows and Linux. The external tools it drives (arm-none-eabi-gcc,
make, the SEGGER J-Link software, arduino-cli) are found on PATH first and in
each platform's usual install locations second.

Launch the GUI:        python3 ht5023_flasher.py     (pythonw on Windows)
                       or double-click the launcher in the project root
Headless build test:   python3 ht5023_flasher.py --selftest [example]
"""

import os
import re
import shutil
import socket
import sys
import time
import subprocess
import threading
from pathlib import Path

LD_NAME = "Cortex-M0"
FLASH_ADDR = 0x00000000

# --------------------------------------------------------------------------
# Platform
#
# The app behaves the same everywhere; only the names and the usual homes of
# the external tools differ. Keeping that in one place lets the rest of the
# file stay plain.
# --------------------------------------------------------------------------
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""            # executable suffix
CREATE_NO_WINDOW = 0x08000000 if IS_WINDOWS else 0


# --------------------------------------------------------------------------
# Project-root discovery (makes the tool independent of its own location)
# --------------------------------------------------------------------------
def find_repo_root(start):
    """Walk up from `start` to find the HT5023 project root (has a Makefile)."""
    p = Path(start).resolve()
    if p.is_file():
        p = p.parent
    for cand in [p, *p.parents]:
        if (cand / "Makefile").exists() and (
            (cand / "sdk").exists() or (cand / "examples").exists()
        ):
            return cand
    return None


# --------------------------------------------------------------------------
# Tool detection
# --------------------------------------------------------------------------
def _first_existing(paths):
    for p in paths:
        p = Path(p)
        if p.exists():
            return p
    return None


def _which(name):
    """Find `name` on PATH as a Path, trying the platform's suffix too."""
    found = shutil.which(name) or (shutil.which(name + EXE) if EXE else None)
    return Path(found) if found else None


def find_gcc_bin():
    """The directory holding arm-none-eabi-gcc, or None.

    PATH first: on Linux the toolchain is normally a distro package, and on
    Windows an installer that offered to add itself. The rest are the places a
    toolchain ends up when nobody put it on PATH.
    """
    on_path = _which("arm-none-eabi-gcc")
    if on_path:
        return on_path.parent

    cands = [Path.home() / ".platformio/packages/toolchain-gccarmnoneeabi/bin"]
    if IS_WINDOWS:
        cands.append(Path("C:/Program Files (x86)/Arm GNU Toolchain/bin"))
        for base in (Path("C:/Program Files/Arm GNU Toolchain"),
                     Path("C:/Program Files (x86)/Arm GNU Toolchain")):
            if base.is_dir():
                cands += sorted((d / "bin" for d in base.iterdir() if d.is_dir()),
                                reverse=True)
    else:
        # An Arm toolchain tarball unpacked by hand; newest version first.
        for base in (Path("/opt"), Path("/usr/local"), Path.home() / ".local",
                     Path.home() / "opt"):
            if base.is_dir():
                for pattern in ("*arm-none-eabi*/bin", "*gcc-arm*/bin",
                                "*arm-gnu-toolchain*/bin"):
                    cands += sorted(base.glob(pattern), reverse=True)
    for c in cands:
        if (c / ("arm-none-eabi-gcc" + EXE)).exists():
            return c
    return None


def find_make():
    on_path = _which("make")
    if on_path:
        return on_path
    if IS_WINDOWS:
        return _first_existing([
            Path("C:/msys64/usr/bin/make.exe"),
            Path("C:/ProgramData/chocolatey/bin/make.exe"),
            Path("C:/Program Files (x86)/GnuWin32/bin/make.exe"),
        ])
    return _first_existing([Path("/usr/bin/make"), Path("/usr/local/bin/make")])


def find_jlink():
    """The J-Link Commander executable, or None.

    SEGGER calls it JLink.exe on Windows and JLinkExe on Linux, where the
    package installs into /opt/SEGGER/JLink and symlinks from /usr/bin.
    """
    if IS_WINDOWS:
        segger = Path("C:/Program Files/SEGGER")
        if segger.is_dir():
            cands = sorted(segger.glob("JLink*/JLink.exe"), reverse=True)
            if cands:
                return cands[0]
        return _first_existing([Path("C:/Program Files/SEGGER/JLink/JLink.exe")])

    # The project root carries a JLinkExe wrapper script for Git Bash; if it is
    # on PATH it must not be mistaken for the real thing.
    on_path = _which("JLinkExe")
    if on_path is not None and os.access(on_path, os.X_OK) \
            and on_path.resolve() != Path(__file__).resolve().parent.parent / "JLinkExe":
        return on_path
    for base in (Path("/opt/SEGGER"), Path("/usr/lib"), Path("/usr/local/lib")):
        if base.is_dir():
            cands = sorted(base.glob("JLink*/JLinkExe"), reverse=True)
            if cands:
                return cands[0]
    return _first_existing([Path("/usr/bin/JLinkExe"),
                            Path("/usr/local/bin/JLinkExe"),
                            Path("/opt/SEGGER/JLink/JLinkExe")])


TOOLCHAIN_HINT = (
    "Install the Arm GNU Toolchain, or let PlatformIO provide it at\n"
    "~/.platformio/packages/toolchain-gccarmnoneeabi/bin."
    if IS_WINDOWS else
    "Install it with your package manager, for example:\n"
    "    Debian/Ubuntu:  sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi\n"
    "    Fedora:         sudo dnf install arm-none-eabi-gcc-cs\n"
    "    Arch:           sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils\n\n"
    "Or run  ./install-linux.sh  in the project folder, which installs\n"
    "everything this app needs in one go.")

MAKE_HINT = (
    "Install MSYS2 (C:/msys64/usr/bin/make.exe) or GNU make for Windows."
    if IS_WINDOWS else
    "Install it with your package manager, for example:\n"
    "    Debian/Ubuntu:  sudo apt install make\n\n"
    "Or run  ./install-linux.sh  in the project folder.")


def detect_tools():
    gcc_bin = find_gcc_bin()
    if gcc_bin is None:
        raise FileNotFoundError("arm-none-eabi-gcc not found.\n\n" + TOOLCHAIN_HINT)

    make_exe = find_make()
    if make_exe is None:
        raise FileNotFoundError(
            ("make.exe not found.\n\n" if IS_WINDOWS else "make not found.\n\n")
            + MAKE_HINT)

    # The J-Link is optional. A board carrying the UART bootloader is programmed
    # and logged over the USB adapter alone, so a missing probe -- or missing
    # SEGGER software -- must not stop the app from starting.
    return {"gcc_bin": gcc_bin, "make": make_exe, "jlink": find_jlink()}


_JLINK_DLL = {}


def jlink_probes(tools):
    """How many J-Link probes are plugged in. None when it cannot be told.

    Asked through the J-Link DLL rather than by running JLink.exe: with no probe
    attached the SEGGER tools open a modal "select emulator" window and sit
    there waiting, which is no way to answer a question -- especially when the
    user is programming over USB and never asked for the J-Link at all.
    """
    import ctypes

    exe = tools.get("jlink")
    if exe is None:
        return 0

    key = str(exe.parent)
    if key not in _JLINK_DLL:
        # x64 first: the plain JLinkARM.dll in a 64-bit Windows install is the
        # 32-bit build and will not load here. Linux ships the same library as
        # libjlinkarm.so, usually a versioned symlink beside the executable --
        # and /usr/bin/JLinkExe is itself a symlink into that directory, so the
        # resolved location has to be searched as well.
        if IS_WINDOWS:
            names = ("JLink_x64.dll", "JLinkARM.dll")
            dirs = [exe.parent]
        else:
            # Globbed rather than listed: SEGGER bumps the soname with the
            # major version (libjlinkarm.so.9 in V9.76), so any fixed list goes
            # stale. Newest first, with the unversioned symlink -- which most
            # installs provide -- sorting to the front.
            names = None
            dirs = [Path(os.path.realpath(exe)).parent, exe.parent,
                    Path("/opt/SEGGER/JLink"), Path("/usr/lib"),
                    Path("/usr/local/lib")]
        dll = None
        for d in dirs:
            if not d.is_dir():
                continue
            if names is None:
                # x86 is the 32-bit build and will not load in a 64-bit process.
                found = sorted((f for f in d.glob("libjlinkarm.so*")
                                if "_x86" not in f.name), reverse=True)
                candidates = [f for f in found if f.name == "libjlinkarm.so"] + found
            else:
                candidates = [d / n for n in names]
            for path in candidates:
                if not path.exists():
                    continue
                try:
                    dll = ctypes.CDLL(str(path))
                    break
                except OSError:
                    dll = None
            if dll is not None:
                break
        _JLINK_DLL[key] = dll

    dll = _JLINK_DLL[key]
    if dll is None:
        return None            # cannot tell; callers carry on as before
    try:
        buf = ctypes.create_string_buffer(8192)
        return max(0, int(dll.JLINKARM_EMU_GetList(1, buf, 8)))  # 1 = USB
    except Exception:
        return None


def jlink_unavailable(tools):
    """Why SWD cannot be used right now, or None when it can."""
    if tools.get("jlink") is None:
        return (f"no J-Link software found ({'JLink.exe' if IS_WINDOWS else 'JLinkExe'})"
                " -- install it, or switch 'via' to USB (bootloader) to program "
                "over the USB adapter")
    if jlink_probes(tools) == 0:
        return ("no J-Link probe is connected -- plug it in, or switch 'via' to "
                "USB (bootloader) to program over the USB adapter")
    return None


def make_env(tools, repo):
    env = os.environ.copy()
    tmp = repo / "build" / "_tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = env["TMPDIR"] = str(tmp)
    # os.pathsep, not ';': Linux separates PATH entries with ':'.
    env["PATH"] = os.pathsep.join(
        [str(tools["gcc_bin"]), str(tools["make"].parent), env.get("PATH", "")])
    return env


def example_from_path(path):
    parts = Path(path).resolve().parts
    if "examples" in parts:
        i = parts.index("examples")
        if i + 1 < len(parts):
            return parts[i + 1]
    return None


# --------------------------------------------------------------------------
# Flash-script generation (HT5023 datasheet flash sequence; mass erase + run)
# --------------------------------------------------------------------------
ERASE_MS = 20    # per-page erase settle (datasheet: 2 ms max)
PAGE_SIZE = 256  # flash page: the program window has to be re-armed per page


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
    bin_path, out_path = Path(bin_path), Path(out_path)
    CMU = 0x4000F000
    WPREG, FLASHCON, FLASHLOCK = CMU + 0x00, CMU + 0x34, CMU + 0x38
    FS_LOCKS = [
        (CMU + 0x60, 0x7161), (CMU + 0x64, 0x7262), (CMU + 0x68, 0x7363),
        (CMU + 0x6C, 0x7464), (CMU + 0x70, 0x7565), (CMU + 0x74, 0x7666),
        (CMU + 0x78, 0x7767), (CMU + 0x7C, 0x7868), (CMU + 0x80, 0x7969),
        (CMU + 0x84, 0x7A6A), (CMU + 0x88, 0x7B6B),
    ]
    data = bin_path.read_bytes()
    if not data:
        raise ValueError("Built binary is empty.")
    if addr % 4:
        raise ValueError("Flash address must be 4-byte aligned.")
    if addr + len(data) > 0x40000:
        raise ValueError("Binary exceeds 256 KiB code flash.")
    opt = 0x0FC0
    if addr <= opt < addr + len(data):
        off = opt - addr
        if data[off:off + 4] != b"\xFF\xFF\xFF\xFF":
            raise ValueError(
                "Image would write 0x%s over the flash option bytes at 0x0FC0. "
                "A non-0xFF value at 0x0FC1 enables read protection and stops the "
                "part being read or debugged until a mass erase. Fix the linker "
                "script so nothing is placed there."
                % data[off:off + 4].hex().upper())
    data += b"\xFF" * ((-len(data)) % 4)

    def unlock():
        return [f"w2 0x{WPREG:08X}, 0xA55A", f"w2 0x{FLASHLOCK:08X}, 0x7A68"]

    # Mass erase (FLASHCON=0x03, datasheet 2.3.2: 10 ms max). FLASHLOCK=0x7A68
    # alone grants erase rights over the whole array; the FSnLOCK partition keys
    # below are for the programming phase and need FLASHLOCK to be non-0x7A68.
    lines = ["r", "halt"] + unlock()
    lines += [f"w1 0x{FLASHCON:08X}, 0x03", f"w4 0x{addr:08X}, 0xFFFFFFFF",
              "sleep 100", f"w1 0x{FLASHCON:08X}, 0x00",
              f"w2 0x{FLASHLOCK:08X}, 0x0000"]
    lines += [f"w2 0x{reg:08X}, 0x{key:04X}" for reg, key in FS_LOCKS]
    lines.append(f"w1 0x{FLASHCON:08X}, 0x01")

    # Words already at the erased value need no write, which skips the padding
    # between the vector table and .text -- including the option-byte word.
    written = 0
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i + 4], "little")
        if word == 0xFFFFFFFF:
            continue
        lines.append(f"w4 0x{addr + i:08X}, 0x{word:08X}")
        written += 1

    lines += [f"w1 0x{FLASHCON:08X}, 0x00", f"w2 0x{FLASHLOCK:08X}, 0x0000",
              f"w2 0x{WPREG:08X}, 0x0000"]

    # Read back checkpoints spread across the image so a partial program cannot
    # pass as success. run_pipeline() compares these against the binary.
    for off in verify_offsets(len(data)):
        lines.append(f"mem32 0x{addr + off:08X}, 4")
    lines += ["r", "g", "q"]
    out_path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    return len(data)


# --------------------------------------------------------------------------
# Subprocess streaming
# --------------------------------------------------------------------------
def run_cmd(cmd, env, cwd, timeout, on_line):
    proc = subprocess.Popen(
        [str(c) for c in cmd], cwd=str(cwd) if cwd else None, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=1, creationflags=CREATE_NO_WINDOW)
    killed = {"v": False}

    def _kill():
        killed["v"] = True
        try:
            proc.kill()
        except Exception:
            pass

    timer = threading.Timer(timeout, _kill)
    timer.start()
    try:
        for line in proc.stdout:
            on_line(line.rstrip("\n"))
    finally:
        try:
            proc.stdout.close()
        except Exception:
            pass
        rc = proc.wait()
        timer.cancel()
    return None if killed["v"] else rc


# --------------------------------------------------------------------------
# Pipeline: build -> generate -> flash
# --------------------------------------------------------------------------
# --------------------------------------------------------------------------
# Arduino sketches (.ino)
# --------------------------------------------------------------------------
ARDUINO_FQBN = "ht5023:ht5023:ht5023"

# The IDE bundles arduino-cli; there is no need to make people install it too.
_IDE_TAIL = "resources/app/lib/backend/resources/arduino-cli"


def _arduino_cli_candidates():
    home = Path.home()
    if IS_WINDOWS:
        return [
            Path(f"C:/Program Files/Arduino IDE/{_IDE_TAIL}.exe"),
            Path(f"C:/Program Files (x86)/Arduino IDE/{_IDE_TAIL}.exe"),
            home / f"AppData/Local/Programs/Arduino IDE/{_IDE_TAIL}.exe",
        ]
    return [
        home / ".local/bin/arduino-cli",
        home / "bin/arduino-cli",
        Path("/usr/local/bin/arduino-cli"),
        Path("/usr/bin/arduino-cli"),
        Path("/snap/bin/arduino-cli"),
        # Arduino IDE 2.x on Linux: the .zip unpacked somewhere, or the
        # AppImage extracted. Both keep arduino-cli at the same relative path.
        Path(f"/opt/arduino-ide/{_IDE_TAIL}"),
        Path(f"/usr/share/arduino-ide/{_IDE_TAIL}"),
        home / f".local/share/arduino-ide/{_IDE_TAIL}",
        home / f"Applications/arduino-ide/{_IDE_TAIL}",
        home / f"arduino-ide/{_IDE_TAIL}",
    ]


ARDUINO_CLI_CANDIDATES = _arduino_cli_candidates()


def is_sketch(path):
    return Path(path).suffix.lower() == ".ino"


def find_arduino_cli():
    return _which("arduino-cli") or _first_existing(ARDUINO_CLI_CANDIDATES)


def arduino_core_installed(cli, env):
    """True once 'HT5023 Board' shows up in arduino-cli's board list."""
    out = []
    run_cmd([cli, "board", "listall"], env, None, 60, out.append)
    return any(ARDUINO_FQBN in l for l in out)


def sketch_folder(path):
    """The folder arduino-cli compiles.

    A sketch has to live in a folder named after its main .ino, so opening
    Blink/Blink.ino compiles Blink/. A stray .ino somewhere else is reported
    rather than silently compiled as something different.
    """
    p = Path(path).resolve()
    return p.parent if p.parent.name == p.stem else None


def run_arduino_pipeline(src_path, tools, repo, log, boot_app=False):
    """Compile a sketch with arduino-cli. Returns the .bin, or None.

    Installs the HT5023 core into the sketchbook first if the IDE does not know
    the board yet, so a fresh machine only has to open a sketch and press the
    button.
    """
    cli = find_arduino_cli()
    if cli is None:
        log("ERROR: arduino-cli not found. Install the Arduino IDE (it ships "
            "arduino-cli) or put arduino-cli on PATH.", "err")
        return None

    folder = sketch_folder(src_path)
    if folder is None:
        p = Path(src_path)
        log(f"ERROR: a sketch must sit in a folder with the same name -- move "
            f"{p.name} into a folder called '{p.stem}'.", "err")
        return None

    env = make_env(tools, repo) if repo else os.environ.copy()

    if not arduino_core_installed(cli, env):
        installer = (repo / "arduino" / "install.py") if repo else None
        if installer and installer.exists():
            log("==> Installing the HT5023 core into the Arduino sketchbook", "head")
            run_cmd([sys.executable, str(installer)], env, repo, 120, log)
        if not arduino_core_installed(cli, env):
            log("ERROR: the HT5023 core is not installed for the Arduino IDE. "
                "Run: python arduino/install.py", "err")
            return None

    layout = "bootloader" if boot_app else "standalone"
    out_dir = (repo / "build" / "ino") if repo else folder / "build"
    out_dir.mkdir(parents=True, exist_ok=True)

    log(f"==> Compiling sketch '{folder.name}' with arduino-cli"
        + ("  [for the UART bootloader, app at 0x2000]" if boot_app
           else "  [standalone, from 0x0000]"), "head")
    rc = run_cmd([cli, "compile", "-b", f"{ARDUINO_FQBN}:layout={layout}",
                  "--output-dir", str(out_dir), str(folder)],
                 env, repo, 300, log)
    if rc != 0:
        log(f"BUILD FAILED (arduino-cli exit {rc}).", "err")
        return None

    binary = out_dir / f"{folder.name}.ino.bin"
    if not binary.exists():
        log(f"BUILD FAILED: {binary.name} was not produced.", "err")
        return None

    # A sketch linked for the wrong layout would be programmed at the wrong
    # address and never run; the reset vector says which one it is.
    head = binary.read_bytes()[:8]
    entry = int.from_bytes(head[4:8], "little") & ~1
    if boot_app and entry < 0x2000:
        log("BUILD FAILED: the sketch was linked for the standalone layout "
            "(entry below 0x2000) -- it cannot be uploaded over USB.", "err")
        return None
    if not boot_app and entry >= 0x2000:
        log("NOTE: this sketch is linked for the bootloader layout (0x2000); "
            "programming it over SWD leaves the bootloader in charge.", "err")

    log(f"Built {binary} ({binary.stat().st_size} bytes).", "ok")
    return binary


def arduino_libraries(cli, env):
    """Installed Arduino libraries, as (name, version) pairs.

    Asked for as JSON: the table arduino-cli prints by default is aligned with
    spaces, and most library names have spaces in them ("Adafruit GFX Library"),
    so splitting the columns by eye turns them into nonsense.
    """
    out = []
    run_cmd([cli, "lib", "list", "--format", "json"], env, None, 90, out.append)
    try:
        import json
        data = json.loads("\n".join(out))
    except Exception:
        return []
    entries = data.get("installed_libraries", data) if isinstance(data, dict) else data
    libs = []
    for e in entries or []:
        lib = e.get("library", e) if isinstance(e, dict) else {}
        name = lib.get("name") or ""
        if name:
            libs.append((name, lib.get("version") or "?"))
    return sorted(libs, key=lambda p: p[0].lower())


def arduino_lib_install(cli, env, name, log):
    log(f"==> Installing Arduino library '{name}'", "head")
    rc = run_cmd([cli, "lib", "install", name], env, None, 300, log)
    if rc == 0:
        log(f"Installed '{name}'. Include it in your sketch and rebuild.", "ok")
        return True
    log(f"Could not install '{name}' (arduino-cli exit {rc}). Check the exact "
        "name, or the network.", "err")
    return False


def install_bootloader(tools, repo, speed, log):
    """Build the UART bootloader and program it at 0x0000 over SWD.

    A one-off: the part has no factory ISP, so the very first program has to
    arrive through the debug probe. Everything after that goes over USB.
    """
    if repo is None:
        log("ERROR: no HT5023 project root found -- open the project folder.", "err")
        return False
    why = jlink_unavailable(tools)
    if why:
        log(f"Installing the bootloader needs the debug probe once: {why}.", "err")
        return False

    boot_bin = repo / "build" / "boot.bin"
    log("==> Building the UART bootloader", "head")
    rc = run_cmd([tools["make"], "bootloader"], make_env(tools, repo), repo, 240, log)
    if rc != 0 or not boot_bin.exists():
        log(f"BUILD FAILED (make bootloader exit {rc}).", "err")
        return False
    log(f"Built {boot_bin} ({boot_bin.stat().st_size} bytes).", "ok")

    if not flash_bin_jlink(boot_bin, tools, speed, repo, log, tag="boot"):
        return False
    log("Bootloader installed. From now on 'via: USB (bootloader)' programs the "
        "board over the USB adapter alone -- the probe is no longer needed.", "ok")
    log("Note: uploading with 'via: J-Link (SWD)' overwrites it, because a "
        "standalone image starts at 0x0000.", "dim")
    return True


SKETCH_TEMPLATE = '''// {name} -- HT5023 Arduino sketch.
//
// Serial is UART2: PC11 = TX, PC12 = RX -- the pins the USB adapter uses, so
// Serial.print() lands in the debug log below.

void setup() {{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println("{name} started");
}}

void loop() {{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}}
'''


def new_sketch(folder, name):
    """Create <folder>/<name>/<name>.ino. Returns the path, or raises."""
    safe = "".join(c for c in name.strip() if c.isalnum() or c in "_-")
    if not safe:
        raise ValueError("a sketch name needs at least one letter or digit")
    # arduino-cli compiles a folder, and the folder has to carry the sketch's
    # name -- a loose .ino somewhere else is not a sketch.
    d = Path(folder) / safe
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{safe}.ino"
    if path.exists():
        raise FileExistsError(f"{path} already exists")
    path.write_text(SKETCH_TEMPLATE.format(name=safe), encoding="utf-8",
                    newline="\n")
    return path


def flash_bin_jlink(app_bin, tools, speed, repo, log, tag="app"):
    """Program a .bin at FLASH_ADDR over SWD and verify it came back.

    Shared by the Makefile examples and the Arduino sketches -- once there is a
    binary, neither cares where it came from.
    """
    why = jlink_unavailable(tools)
    if why:
        log(f"FLASH FAILED: {why}.", "err")
        return False

    env = make_env(tools, repo)
    flash_script = repo / "build" / f"flash_{tag}.jlink"
    flash_script.parent.mkdir(parents=True, exist_ok=True)
    log("==> Generating J-Link flash script", "head")
    try:
        n = generate_flash_script(app_bin, FLASH_ADDR, flash_script)
    except Exception as e:
        log(f"FLASH-SCRIPT FAILED: {e}", "err")
        return False
    log(f"Wrote {flash_script.name} ({n} bytes of code).", "ok")

    log(f"==> Flashing over SWD via {tools['jlink'].name}", "head")
    captured = []

    def _flashlog(l):
        captured.append(l)
        log(l)

    rc = run_cmd([tools["jlink"], "-device", LD_NAME, "-if", "SWD", "-speed", str(speed),
                  "-autoconnect", "1", "-CommanderScript", flash_script],
                 env, repo, 90, _flashlog)
    text = "\n".join(captured)
    if rc is None:
        log("FLASH FAILED: J-Link did not finish in time -- is the probe plugged "
            "in and the target powered (VTref)?", "err")
        return False
    if "Cannot connect" in text or "Could not connect" in text or "FAILED" in text:
        log("FLASH FAILED: J-Link could not connect to the target.", "err")
        return False
    if "Script processing completed." not in text:
        log("FLASH FAILED: J-Link did not finish the script.", "err")
        return False

    # Compare every checkpoint J-Link read back against the image itself.
    image = Path(app_bin).read_bytes()
    readbacks = {}
    for m in re.finditer(r"^([0-9A-Fa-f]{8}) = ((?:[0-9A-Fa-f]{8} ?)+)", text, re.M):
        readbacks[int(m.group(1), 16)] = m.group(2).split()
    checked = bad = 0
    for address, words in readbacks.items():
        off = address - FLASH_ADDR
        if off < 0 or off + 4 * len(words) > len(image):
            continue
        for i, w in enumerate(words):
            expect = int.from_bytes(image[off + 4 * i:off + 4 * i + 4], "little")
            checked += 1
            if int(w, 16) != expect:
                if bad == 0:
                    log(f"VERIFY FAILED at 0x{address + 4 * i:08X}: flash has "
                        f"0x{int(w, 16):08X}, image has 0x{expect:08X}.", "err")
                bad += 1
    if bad:
        log(f"FLASH FAILED: {bad} of {checked} verified words differ -- the image "
            "was only partially programmed.", "err")
        return False
    if checked == 0:
        log("FLASH uncertain: no readback to verify against.", "err")
        return False
    log(f"UPLOAD OK -- {checked} words verified, target reset and running.", "ok")
    return True


def run_pipeline(src_path, tools, speed, repo, log, flash=True, boot_app=False):
    if repo is None:
        log("ERROR: no HT5023 project root found (no Makefile above this file). "
            "Open the HT5023 project folder.", "err")
        return False
    env = make_env(tools, repo)
    build = repo / "build"
    example = example_from_path(src_path)
    if not example:
        log("ERROR: file is not under examples/<name>/main.c -- the Makefile "
            "builds examples/<EXAMPLE>/main.c.", "err")
        return False

    log(f"==> Building example '{example}'"
        + ("  [for the UART bootloader, app at 0x2000]" if boot_app else "")
        + f"  (repo: {repo})", "head")
    make_cmd = [tools["make"], f"EXAMPLE={example}"]
    if boot_app:
        make_cmd.append("BOOT_APP=1")
    make_cmd.append("build")
    rc = run_cmd(make_cmd, env, repo, 180, lambda l: log(l))
    if rc != 0:
        log(f"BUILD FAILED (make exit {rc}).", "err")
        return False
    app_bin = build / "app.bin"
    if not app_bin.exists():
        log("BUILD FAILED: build/app.bin not produced.", "err")
        return False
    # Guard against a stale link: every example shares build/app.elf, so a
    # skipped relink would silently flash the previously built example.
    try:
        linked = (build / "app.map").read_text(errors="ignore")
    except OSError:
        linked = ""
    if linked and f"examples/{example}/main.o" not in linked.replace("\\", "/"):
        log(f"BUILD FAILED: build/app.elf does not contain examples/{example}/main.o "
            "-- the link was skipped (stale binary). Run 'make clean' and retry.", "err")
        return False
    log(f"Built {app_bin} ({app_bin.stat().st_size} bytes).", "ok")

    # An empty debug log is almost always this: the example never writes to a
    # UART and never arms semihosting, so there is nothing to show.
    syms = elf_symbols(build / "app.elf")
    talks_uart = any(s.startswith("ht5023_uart_write") for s in syms)
    talks_semihost = ("g_semihost_enable" in syms) or ("g_force_semihost" in syms)
    if not talks_uart and not talks_semihost:
        log(f"NOTE: '{example}' produces no serial output (no UART writes, no "
            "semihosting) -- the debug log will stay empty. Try usb_echo, "
            "text_back or random_sentence to see the log working.", "err")
    elif boot_app and not talks_uart:
        # Uploading over USB means there is no debugger, so a semihosting-only
        # example has nowhere to print.
        log(f"NOTE: '{example}' only logs over semihosting (the J-Link). Uploaded "
            "over USB there is no debugger attached, so the log will stay empty. "
            "Use usb_echo or text_back to test the USB link.", "err")

    if not flash:
        log("Built only -- no J-Link programming."
            + (" The image will be sent over USB." if boot_app else ""), "head")
        return True

    return flash_bin_jlink(app_bin, tools, speed, repo, log, tag=example)


# --------------------------------------------------------------------------
# Live-output helpers
# --------------------------------------------------------------------------
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b[@-_]")


def clean_stream_text(text):
    """Strip ANSI/CR noise so raw target output stays readable in the log."""
    text = ANSI_RE.sub("", text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text.replace("\x00", "")


# Descriptions the usual USB-UART bridges report. Windows and Linux word them
# differently ("USB-SERIAL CH340" vs "USB2.0-Serial"), so both are listed.
USB_UART_HINTS = ("ftdi", "ft232", "ft231", "ft230", "ft2232", "cp210", "cp21",
                  "ch340", "ch341", "ch910", "pl2303", "usb serial", "usb-serial",
                  "usb uart", "usb-uart", "silicon labs cp", "usb2.0-serial",
                  "usb single serial")

# Linux names a port after the kernel driver behind it, which says more than
# the description does: ttyUSB* is a USB-serial bridge and ttyACM* a USB CDC
# device, while ttyS* is a motherboard UART. Several ttyS* nodes exist on most
# machines with nothing wired to them, so one must never be picked
# automatically -- the log would sit silent on a port that can never speak.
LINUX_USB_PREFIXES = ("/dev/ttyusb", "/dev/ttyacm")
LINUX_BUILTIN_RE = re.compile(r"^/dev/ttyS\d+$")


def port_rank(description, device=""):
    """0 = a real USB-UART bridge, 1 = some other port, 2 = never the target."""
    d = (description or "").lower()
    dev = (device or "").lower()
    if "bluetooth" in d or "bthenum" in d or "rfcomm" in dev:
        return 2
    if any(h in d for h in USB_UART_HINTS):
        return 0
    if dev.startswith(LINUX_USB_PREFIXES):
        return 0
    if LINUX_BUILTIN_RE.match(device or ""):
        return 2
    return 1


def port_kind(description, device=""):
    """Short label for the chip behind a port, for the UI."""
    d = (description or "").lower()
    dev = (device or "").lower()
    for name, hints in (("FTDI", ("ftdi", "ft232", "ft231", "ft230", "ft2232")),
                        ("CP210x", ("cp210", "cp21", "silicon labs cp")),
                        ("CH340", ("ch340", "ch341", "ch910")),
                        ("PL2303", ("pl2303",))):
        if any(h in d for h in hints):
            return name
    if any(h in d for h in ("usb serial", "usb-serial", "usb uart", "usb-uart",
                            "usb2.0-serial", "usb single serial")):
        return "USB-UART"
    if "bluetooth" in d or "bthenum" in d or "rfcomm" in dev:
        return "Bluetooth"
    if dev.startswith("/dev/ttyusb"):
        return "USB-UART"
    if dev.startswith("/dev/ttyacm"):
        return "USB CDC"
    if LINUX_BUILTIN_RE.match(device or ""):
        return "built-in"
    return "serial"


def is_likely_target_port(description, device=""):
    """Bluetooth and motherboard ports are never the USB adapter we are after."""
    return port_rank(description, device) < 2


def is_phantom_port(device, hwid):
    """True for a /dev/ttyS* node the kernel registered with nothing behind it.

    The 8250 driver creates ttyS0..ttyS31 on most PCs whether or not the chips
    exist, so an unfiltered list is 32 dead entries deep and the one real
    adapter is impossible to find. A port that is genuinely there is announced
    by firmware and carries a hardware id ("PNP0501"); the phantoms report
    nothing at all.
    """
    if not LINUX_BUILTIN_RE.match(device or ""):
        return False
    return (hwid or "").strip().lower() in ("", "n/a")


def list_serial_ports():
    """[(device, description), ...] with likely target adapters first."""
    try:
        from serial.tools import list_ports
    except Exception:
        return []
    ports = [(p.device, (p.description or "").strip())
             for p in list_ports.comports()
             if not is_phantom_port(p.device, p.hwid)]
    ports.sort(key=lambda d: (port_rank(d[1], d[0]), d[0]))
    return ports


def serial_group():
    """The group that owns serial devices on this distro."""
    try:
        import grp
    except ImportError:
        return "dialout"
    for name in ("dialout", "uucp", "plugdev"):
        try:
            grp.getgrnam(name)
            return name
        except KeyError:
            continue
    return "dialout"


def serial_permission_problem(ports):
    """A message when ports exist but this user is not allowed to open them.

    The single most common reason the app looks broken on a fresh Linux
    install: the adapter is plugged in and listed, but opening it fails until
    the user is in the serial group -- and that needs a fresh login to take
    effect, which is not obvious from "permission denied".
    """
    if IS_WINDOWS or not ports:
        return ""
    blocked = [d for d, _ in ports
               if os.path.exists(d) and not os.access(d, os.R_OK | os.W_OK)]
    if not blocked:
        return ""
    group = serial_group()
    return (f"No permission to open {', '.join(blocked)}. Serial devices belong "
            f"to the '{group}' group -- run  sudo usermod -aG {group} $USER  "
            "and then log out and back in.")


def usb_upload(port, baud, app_bin, log, progress=None, do_reset=False,
               start_after=True):
    """Program a built image through the UART bootloader. Returns "" on success."""
    try:
        import ht5023_uart_flash as boot
    except Exception as e:
        return f"cannot load the bootloader protocol module: {e}"
    try:
        image = Path(app_bin).read_bytes()
    except OSError as e:
        return f"cannot read {app_bin}: {e}"
    if not image:
        return "the built image is empty"
    log(f"==> Uploading {len(image)} bytes over {port} @ {baud} via the bootloader",
        "head")
    return boot.upload(port, baud, image, log=lambda m: log("   " + m),
                       progress=progress, do_reset=do_reset,
                       run_after=start_after)


def boot_start_app(port, baud, log):
    """Tell a waiting bootloader to start the application it is holding."""
    try:
        import serial
    except Exception as e:
        return f"pyserial missing: {e}"
    try:
        with serial.Serial(port, int(baud), timeout=0.3) as ser:
            ser.write(b"J\r\n")
            time.sleep(0.2)
        return ""
    except Exception as e:
        return str(e)


def wait_for_unlocked(path, timeout=4.0):
    """Wait until `path` can be opened for writing.

    Windows frees a killed process's file handles asynchronously, so the linker
    can still hit 'Permission denied' right after stopping the monitor. Linux
    has no such delay; there the loop simply succeeds on its first pass.
    """
    path = Path(path)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if not path.exists():
                return True
            with open(path, "ab"):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def serial_send(handles, data):
    """Write bytes to whichever link the monitor has open. Returns (ok, error).

    Serial goes straight out of the serial port. Semihosting goes to the J-Link GDB
    server's terminal channel, which is what the target's SYS_READC reads from —
    so a board with only the debug cable can still be typed at.
    """
    handles = handles or {}
    ser = handles.get("serial")
    if ser is not None:
        try:
            ser.write(data)
            return True, ""
        except Exception as e:
            return False, str(e)
    sock = handles.get("telnet")
    if sock is not None:
        try:
            sock.sendall(data)
            return True, ""
        except Exception as e:
            return False, str(e)
    return False, "no serial port or semihosting channel is open"


def _kill_all(procs):
    for p in procs:
        try:
            p.kill()
        except Exception:
            pass


# --------------------------------------------------------------------------
# Serial monitor (UART2 on PC11/PC12 -> USB-TTL adapter), byte-level streaming
# --------------------------------------------------------------------------
def run_serial_monitor(port, baud, log, log_raw, handles, stop):
    try:
        import serial
    except Exception:
        log("ERROR: the serial monitor needs pyserial  ->  pip install pyserial", "err")
        return
    try:
        ser = serial.Serial(port, int(baud), timeout=0.05)
    except Exception as e:
        detail = str(e)
        log(f"ERROR: cannot open {port}: {detail}", "err")
        low = detail.lower()
        if "denied" in low or "permissionerror" in low:
            if IS_WINDOWS:
                log(f"       {port} is already open in another program -- close the "
                    "other serial terminal (SerialDebug, PuTTY, Arduino monitor, ...) "
                    "and retry.", "err")
            else:
                group = serial_group()
                log(f"       Either {port} is open in another program (close the "
                    "other serial terminal and retry), or this user is not allowed "
                    "to use it.", "err")
                log(f"       On Linux serial devices belong to the '{group}' group. "
                    "Join it once with:", "err")
                log(f"           sudo usermod -aG {group} $USER", "err")
                log("       then log out and back in -- the group is only picked up "
                    "at login.", "err")
        elif "cannot find" in low or "filenotfound" in low:
            available = [d for d, _ in list_serial_ports()]
            log(f"       {port} no longer exists -- the adapter was unplugged. "
                + (f"Available now: {', '.join(available)}." if available
                   else "No serial ports are present."), "err")
        return
    handles["serial"] = ser
    log(f"==> Serial monitor on {port} @ {baud} 8N1 -- target output below:", "head")
    try:
        while not stop.is_set():
            try:
                data = ser.read(max(1, ser.in_waiting))
            except Exception as e:
                if not stop.is_set():
                    log(f"Serial read failed: {e}", "err")
                break
            if data:
                log_raw(data.decode("utf-8", "replace"))
    finally:
        handles.pop("serial", None)
        try:
            ser.close()
        except Exception:
            pass
    log("Serial monitor stopped.", "head")


# --------------------------------------------------------------------------
# Semihosting monitor (J-Link GDB server + gdb).
# Output is read from the GDB server's semihosting telnet channel rather than
# gdb's stdout: gdb block-buffers its console when piped, which delays -- and
# swallows partial lines from -- a live debug log.
# --------------------------------------------------------------------------
GDB_PORT = 2331
SEMIHOST_TELNET_PORT = 2333


def find_gdb(tools):
    """arm-none-eabi-gdb, from the toolchain directory or from PATH.

    Debian dropped arm-none-eabi-gdb from its gcc-arm-none-eabi package;
    gdb-multiarch is the packaged replacement and speaks the same protocol, so
    it is accepted as a fallback.
    """
    p = tools["gcc_bin"] / ("arm-none-eabi-gdb" + EXE)
    if p.exists():
        return p
    for name in ("arm-none-eabi-gdb", "gdb-multiarch"):
        found = _which(name)
        if found:
            return found
    return None


# SEGGER's command-line GDB server: 'JLinkGDBServerCL.exe' on Windows,
# 'JLinkGDBServerCLExe' (no dot) on Linux.
GDBSERVER_NAMES = (("JLinkGDBServerCL.exe", "JLinkGDBServer.exe") if IS_WINDOWS
                   else ("JLinkGDBServerCLExe", "JLinkGDBServer"))


def find_gdbserver(tools):
    jlink = tools.get("jlink")
    if jlink is not None:
        # /usr/bin/JLinkExe is a symlink; the server sits next to the real one.
        dirs = [jlink.parent, Path(os.path.realpath(jlink)).parent]
        for d in dirs:
            for name in GDBSERVER_NAMES:
                if (d / name).exists():
                    return d / name
    for name in GDBSERVER_NAMES:
        found = _which(name)
        if found:
            return found
    return None


def elf_symbols(path):
    """Names defined in an ELF32 little-endian symbol table (no deps, no nm)."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return set()
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        return set()

    def u16(o):
        return int.from_bytes(data[o:o + 2], "little")

    def u32(o):
        return int.from_bytes(data[o:o + 4], "little")

    shoff, shentsize, shnum = u32(0x20), u16(0x2E), u16(0x30)
    names = set()
    for i in range(shnum):
        sh = shoff + i * shentsize
        if sh + 40 > len(data) or u32(sh + 4) != 2:  # SHT_SYMTAB
            continue
        off, size, link, entsize = u32(sh + 16), u32(sh + 20), u32(sh + 24), u32(sh + 36)
        strh = shoff + link * shentsize
        if entsize < 16 or strh + 40 > len(data):
            continue
        stroff, strsize = u32(strh + 16), u32(strh + 20)
        strtab = data[stroff:stroff + strsize]
        for s in range(off, min(off + size, len(data)) - entsize + 1, entsize):
            n = u32(s)
            if 0 < n < len(strtab):
                end = strtab.find(b"\x00", n)
                names.add(strtab[n:end if end >= 0 else None].decode("ascii", "replace"))
    return names


def build_monitor_gdb(elf, io_client):
    """GDB batch script: attach, enable semihosting, arm the target's opt-in flag.

    Examples that gate semihosting behind `g_semihost_enable` (so a plain
    flash-and-run can never hit an unserviced `bkpt 0xAB`) get the flag set once
    execution reaches main() -- before that, startup's .data copy would undo it.
    """
    lines = ["set pagination off", "set confirm off",
             f"target remote localhost:{GDB_PORT}",
             "monitor reset",
             "monitor semihosting enable",
             f"monitor semihosting IOClient {io_client}"]
    # Examples gate semihosting behind a flag the debugger has to arm, so that a
    # plain flash-and-run can never hit an unserviced `bkpt 0xAB`. Arm whichever
    # flag this image defines (the older semihosting examples use a different
    # name), once execution reaches main() -- before that, startup's .bss clear
    # would undo it.
    present = elf_symbols(elf)
    flags = [f for f in ("g_semihost_enable", "g_force_semihost") if f in present]
    if flags:
        # The build carries no DWARF, so gdb knows each address but not its type:
        # `set var g_semihost_enable = 1` fails with "has unknown type", and that
        # error aborts the rest of a -batch script -- leaving the target halted at
        # main() and looking like semihosting is dead. Writing through a cast
        # pointer needs only the symbol's address, which minimal symbols provide.
        lines += ["tbreak main", "continue"]
        lines += [f"set var *(volatile int *)&{f} = 1" for f in flags]
    lines.append("continue")
    return "\n".join(lines) + "\n"


def connect_semihost_telnet(stop, timeout=6.0):
    """Open the GDB server's semihosting telnet channel (raw byte stream)."""
    deadline = time.time() + timeout
    while time.time() < deadline and not stop.is_set():
        try:
            s = socket.create_connection(("127.0.0.1", SEMIHOST_TELNET_PORT), timeout=1.0)
            s.settimeout(0.2)
            return s
        except OSError:
            time.sleep(0.2)
    return None


def _drain_gdb_console(proc, log):
    """Forward gdb's own chatter (breakpoints, errors) as dim log lines."""
    try:
        for line in proc.stdout:
            line = line.rstrip("\n")
            if line.strip():
                log(line, "dim")
    except Exception:
        pass


def jlink_reset_run(tools, repo, speed, log):
    """Reset + run the target after a semihosting session.

    The examples keep their semihosting opt-in flag in RAM. If the debugger goes
    away while that flag is still set, the next `bkpt 0xAB` has nobody to service
    it and the core stops dead. A reset clears the flag through normal startup.
    """
    if jlink_unavailable(tools):
        return
    script = repo / "build" / "reset_run.jlink"
    try:
        script.write_text("r\ng\nq\n", encoding="ascii", newline="\n")
        run_cmd([tools["jlink"], "-device", LD_NAME, "-if", "SWD", "-speed", str(speed),
                 "-autoconnect", "1", "-CommanderScript", script],
                make_env(tools, repo), repo, 30, lambda l: None)
        log("Target reset -- running standalone (semihosting disarmed).", "dim")
    except Exception as e:
        log(f"Could not reset the target after the monitor: {e}", "dim")


def run_monitor(tools, repo, speed, log, log_raw, handles, stop):
    """Run the target under gdb and stream its semihosting output live."""
    # Checked before anything SEGGER is started: with no probe attached their
    # tools raise a modal emulator-selection window instead of failing, and the
    # log has a perfectly good serial source to fall back to.
    why = jlink_unavailable(tools)
    if why:
        log(f"ERROR: semihosting runs over the J-Link, and {why}.", "err")
        log("Set the debug log source to Serial to read the board over USB.", "err")
        return
    gdb = find_gdb(tools)
    gdbserver = find_gdbserver(tools)
    if gdb is None or gdbserver is None:
        log(f"ERROR: semihosting needs arm-none-eabi-gdb and "
            f"{GDBSERVER_NAMES[0]}.", "err")
        return
    if repo is None:
        log("ERROR: no HT5023 project root found -- open the project folder.", "err")
        return
    elf = repo / "build" / "app.elf"
    if not elf.exists():
        log("ERROR: build/app.elf not found -- build first.", "err")
        return
    env = make_env(tools, repo)

    srvlog = repo / "build" / "_tmp" / "gdbserver.log"
    srvf = open(srvlog, "w+", encoding="utf-8", errors="ignore")
    srv = subprocess.Popen(
        [str(gdbserver), "-device", LD_NAME, "-if", "SWD", "-speed", str(speed),
         "-port", str(GDB_PORT), "-singlerun"],
        cwd=str(repo), env=env, stdout=srvf, stderr=subprocess.STDOUT,
        creationflags=CREATE_NO_WINDOW)
    handles["srv"] = srv
    log("==> Starting J-Link GDB server (semihosting)...", "head")

    ready = False
    for _ in range(40):
        if stop.is_set() or srv.poll() is not None:
            break
        time.sleep(0.25)
        try:
            txt = srvlog.read_text(errors="ignore")
        except OSError:
            txt = ""
        if "Waiting for GDB" in txt or "Listening on TCP" in txt:
            ready = True
            break
    if not ready:
        log("ERROR: GDB server did not start -- is the J-Link connected and the "
            "target powered (VTref)?", "err")
        _kill_all((srv,))
        srvf.close()
        return

    # Telnet available -> semihosting goes there only (IOClient 1) and gdb's
    # console stays chatter. Otherwise fall back to gdb's console (IOClient 2).
    sock = None if stop.is_set() else connect_semihost_telnet(stop)
    script = repo / "build" / "semihost_monitor.gdb"
    gdb_body = build_monitor_gdb(elf, 1 if sock else 2)
    script.write_text(gdb_body, encoding="ascii", newline="\n")
    armed = "set var" in gdb_body

    proc = subprocess.Popen(
        [str(gdb), "-nx", "-batch", "-x", str(script), str(elf)],
        cwd=str(repo), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=1, creationflags=CREATE_NO_WINDOW)
    handles["proc"] = proc
    log("==> Semihosting console -- target output below:", "head")

    try:
        if sock is None:
            log("(semihosting telnet channel unavailable -- falling back to gdb's "
                "line-buffered console)", "dim")
            for line in proc.stdout:
                log_raw(line)
                if stop.is_set():
                    break
        else:
            handles["telnet"] = sock      # the send box types through this
            threading.Thread(target=_drain_gdb_console, args=(proc, log),
                             daemon=True).start()
            while not stop.is_set():
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    if proc.poll() is not None and srv.poll() is not None:
                        break
                    continue
                except OSError:
                    break
                if not data:
                    break
                log_raw(data.decode("utf-8", "replace"))
    finally:
        handles.pop("telnet", None)
        if sock is not None:
            try:
                sock.close()
            except Exception:
                pass
        _kill_all((proc, srv))
        try:
            srvf.close()
        except Exception:
            pass
    log("Semihosting monitor stopped.", "head")
    if armed and not handles.get("skip_reset"):
        jlink_reset_run(tools, repo, speed, log)


# --------------------------------------------------------------------------
# GUI
# --------------------------------------------------------------------------
def main_gui():
    import tkinter as tk
    from tkinter import (ttk, filedialog, messagebox, simpledialog,
                         font as tkfont)

    BG, FG = "#1e1e1e", "#d4d4d4"
    SIDE_BG = "#252526"
    GUTTER_BG, GUTTER_FG = "#1e1e1e", "#858585"
    CUR_LINE = "#2a2d2e"
    PANEL, ACCENT = "#252526", "#0e639c"
    C_COMMENT, C_STRING = "#6A9955", "#CE9178"
    C_KW, C_TYPE, C_NUM, C_PP = "#569CD6", "#4EC9B0", "#B5CEA8", "#C586C0"

    KEYWORDS = r"\b(if|else|for|while|do|switch|case|default|break|continue|return|" \
               r"goto|sizeof|typedef|struct|union|enum|static|const|volatile|extern|" \
               r"register|inline|signed|unsigned|__asm__|asm)\b"
    TYPES = r"\b(void|char|short|int|long|float|double|_Bool|bool|" \
            r"uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|size_t)\b"
    SRC_EXT = {".c", ".h", ".cpp", ".cc", ".hpp", ".ino", ".s", ".ld", ".md",
               ".txt", ".json", ".ini", ".py"}
    SKIP_DIRS = {".git", "__pycache__", "_tmp", ".vs"}

    try:
        tools = detect_tools()
    except Exception as e:
        r = tk.Tk(); r.withdraw()
        messagebox.showerror("HT5023 Flasher - missing tools", str(e))
        return

    root = tk.Tk()
    root.title("HT5023 One-Click Flasher")
    root.geometry("1260x820")
    root.configure(bg=BG)

    # Consolas and Segoe UI ship with Windows and exist nowhere else. Tk does
    # not report a missing family -- it silently substitutes, and what it falls
    # back to for a UI font is an unscaled bitmap face that makes the whole
    # window look broken. Pick from what is actually installed instead.
    def _pick_family(candidates):
        have = {f.lower() for f in tkfont.families()}
        for c in candidates:
            if c.lower() in have:
                return c
        return tkfont.nametofont("TkDefaultFont").actual("family")

    MONO_FAMILY = _pick_family((
        "Consolas", "Cascadia Mono", "JetBrains Mono", "Fira Mono",
        "DejaVu Sans Mono", "Liberation Mono", "Ubuntu Mono", "Noto Sans Mono",
        "Menlo", "Courier New", "monospace"))
    UI_FAMILY = _pick_family((
        "Segoe UI", "Cantarell", "Ubuntu", "Noto Sans", "DejaVu Sans",
        "Liberation Sans", "Helvetica Neue", "Arial"))

    def UI(size, *rest):
        return (UI_FAMILY, size, *rest)

    def MONO(size, *rest):
        return (MONO_FAMILY, size, *rest)

    state = {"path": None, "repo": None, "tools": tools, "busy": False,
             "workspace": None, "monitoring": False, "mon_stop": None,
             "mon_handles": {}, "mon_thread": None, "mon_source": None,
             "tree_root": None, "saved_text": ""}
    mono = tkfont.Font(family=MONO_FAMILY, size=11)

    style = ttk.Style()
    try:
        style.theme_use("clam")
    except Exception:
        pass
    style.configure("Side.Treeview", background=SIDE_BG, foreground="#cccccc",
                    fieldbackground=SIDE_BG, borderwidth=0, rowheight=22,
                    font=UI(10))
    style.map("Side.Treeview", background=[("selected", "#094771")],
              foreground=[("selected", "white")])
    style.layout("Side.Treeview", [("Side.Treeview.treearea", {"sticky": "nswe"})])
    style.configure("Dark.TCombobox", fieldbackground=BG, background=PANEL,
                    foreground=FG, arrowcolor="#cccccc", bordercolor="#3a3d41",
                    lightcolor=PANEL, darkcolor=PANEL, padding=2)
    style.map("Dark.TCombobox",
              fieldbackground=[("readonly", BG)], foreground=[("readonly", FG)],
              selectbackground=[("readonly", BG)], selectforeground=[("readonly", FG)])
    root.option_add("*TCombobox*Listbox.background", BG)
    root.option_add("*TCombobox*Listbox.foreground", FG)
    root.option_add("*TCombobox*Listbox.selectBackground", "#094771")
    root.option_add("*TCombobox*Listbox.selectForeground", "white")

    # ---------------- toolbar ----------------
    bar = tk.Frame(root, bg=PANEL)
    bar.pack(side="top", fill="x")

    def mkbtn(text, cmd, bg=ACCENT):
        b = tk.Button(bar, text=text, command=cmd, bg=bg, fg="white",
                      activebackground="#1177bb", activeforeground="white",
                      relief="flat", padx=12, pady=6, font=UI(10, "bold"),
                      cursor="hand2", borderwidth=0)
        b.pack(side="left", padx=(8, 0), pady=8)
        return b

    folder_btn = mkbtn("Open Folder", lambda: open_folder(), bg="#3a3d41")
    open_btn = mkbtn("Open File", lambda: open_file(), bg="#3a3d41")
    save_btn = mkbtn("Save", lambda: save_file(), bg="#3a3d41")
    flash_btn = mkbtn("Compile & Upload (J-Link)", lambda: do_flash())

    tk.Label(bar, text="via", bg=PANEL, fg=FG, font=UI(9)).pack(side="left",
                                                                         padx=(10, 4))
    upload_var = tk.StringVar(value="J-Link (SWD)")
    upload_box = ttk.Combobox(bar, textvariable=upload_var, width=17, state="readonly",
                              style="Dark.TCombobox",
                              values=("J-Link (SWD)", "USB (bootloader)"))
    upload_box.pack(side="left", pady=8)

    tk.Label(bar, text="SWD speed", bg=PANEL, fg=FG, font=UI(9)).pack(side="left", padx=(16, 4))
    speed_var = tk.StringVar(value="1000")
    tk.Spinbox(bar, from_=100, to=4000, increment=100, width=6, textvariable=speed_var,
               bg=BG, fg=FG, insertbackground=FG, relief="flat",
               buttonbackground=PANEL).pack(side="left")
    tk.Label(bar, text="kHz", bg=PANEL, fg=FG, font=UI(9)).pack(side="left", padx=(2, 8))

    path_lbl = tk.Label(bar, text="(no file)", bg=PANEL, fg="#9cdcfe",
                        font=MONO(9), anchor="e")
    path_lbl.pack(side="right", padx=12)

    # ---------------- Arduino row ----------------
    # Everything a sketch needs without leaving the window: start one, add a
    # library, and put the bootloader on the board so uploads go over USB.
    abar = tk.Frame(root, bg=PANEL)
    abar.pack(side="top", fill="x")
    tk.Label(abar, text="Arduino", bg=PANEL, fg="#9cdcfe",
             font=UI(9, "bold")).pack(side="left", padx=(12, 6), pady=(0, 8))

    def mkabtn(text, cmd):
        b = tk.Button(abar, text=text, command=cmd, bg="#3a3d41", fg="white",
                      activebackground="#1177bb", activeforeground="white",
                      relief="flat", padx=10, pady=4, font=UI(9),
                      cursor="hand2", borderwidth=0)
        b.pack(side="left", padx=(0, 6), pady=(0, 8))
        return b

    sketch_btn = mkabtn("New Sketch", lambda: do_new_sketch())
    lib_btn = mkabtn("Libraries…", lambda: do_libraries())
    boot_btn = mkabtn("Install Bootloader (J-Link, once)",
                      lambda: do_install_bootloader())

    autoreset_var = tk.BooleanVar(value=False)
    tk.Checkbutton(abar, text="adapter resets the board (DTR/RTS → RST)",
                   variable=autoreset_var, bg=PANEL, fg=FG, selectcolor=BG,
                   activebackground=PANEL, activeforeground=FG,
                   font=UI(9), relief="flat",
                   highlightthickness=0).pack(side="left", padx=(6, 0), pady=(0, 8))

    # ---------------- main split: sidebar | editor ----------------
    split = tk.PanedWindow(root, orient="horizontal", bg="#333333", sashwidth=4,
                           bd=0, sashrelief="flat")
    split.pack(side="top", fill="both", expand=True)

    # ----- sidebar -----
    side = tk.Frame(split, bg=SIDE_BG, width=300)
    side.pack_propagate(False)
    head = tk.Frame(side, bg=SIDE_BG)
    head.pack(side="top", fill="x")
    tk.Label(head, text="EXPLORER", bg=SIDE_BG, fg="#bbbbbb",
             font=UI(8, "bold"), anchor="w").pack(side="left", padx=10, pady=6)
    tk.Button(head, text="\u21bb Refresh", command=lambda: refresh_explorer(),
              bg=SIDE_BG, fg="#cccccc", activebackground="#37373d",
              activeforeground="white", relief="flat", padx=8, pady=1,
              font=UI(8), cursor="hand2", borderwidth=0).pack(side="right",
                                                                       padx=(0, 8))
    ws_lbl = tk.Label(side, text="No folder opened", bg=SIDE_BG, fg="#cccccc",
                      font=UI(9, "bold"), anchor="w")
    ws_lbl.pack(side="top", fill="x", padx=10)

    treewrap = tk.Frame(side, bg=SIDE_BG)
    treewrap.pack(side="top", fill="both", expand=True)
    tree = ttk.Treeview(treewrap, style="Side.Treeview", show="tree", selectmode="browse")
    tree.pack(side="left", fill="both", expand=True)
    tsb = tk.Scrollbar(treewrap, command=tree.yview)
    tsb.pack(side="right", fill="y")
    tree.configure(yscrollcommand=tsb.set)
    split.add(side, minsize=160)

    node_info = {}  # item_id -> (path, is_dir)

    def add_node(parent, path, is_dir):
        name = Path(path).name + ("/" if is_dir else "")
        nid = tree.insert(parent, "end", text="  " + name, open=False)
        node_info[nid] = (str(path), is_dir)
        if is_dir:
            tree.insert(nid, "end", text="…")  # dummy -> shows expand arrow
        return nid

    def populate(node_id, path):
        for c in tree.get_children(node_id):
            tree.delete(c)
            node_info.pop(c, None)
        try:
            entries = sorted(os.scandir(path),
                             key=lambda e: (not e.is_dir(), e.name.lower()))
        except OSError:
            return
        for e in entries:
            if e.name in SKIP_DIRS:
                continue
            if e.is_dir():
                add_node(node_id, e.path, True)
            elif Path(e.name).suffix.lower() in SRC_EXT or "." not in e.name:
                add_node(node_id, e.path, False)

    def on_open(_=None):
        nid = tree.focus()
        info = node_info.get(nid)
        if not info or not info[1]:
            return
        kids = tree.get_children(nid)
        if len(kids) == 1 and node_info.get(kids[0]) is None:  # dummy
            populate(nid, info[0])

    def on_activate(_=None):
        nid = tree.focus()
        info = node_info.get(nid)
        if not info:
            return
        path, is_dir = info
        if is_dir:
            if tree.item(nid, "open"):
                tree.item(nid, open=False)
            else:
                on_open()
                tree.item(nid, open=True)
        else:
            load_path(path)

    tree.bind("<<TreeviewOpen>>", on_open)
    tree.bind("<Double-1>", on_activate)
    tree.bind("<Return>", on_activate)

    def set_workspace(folder):
        folder = Path(folder).resolve()
        state["workspace"] = folder
        tree.delete(*tree.get_children())
        node_info.clear()
        root_id = add_node("", folder, True)
        state["tree_root"] = root_id
        ws_lbl.configure(text=folder.name.upper())
        populate(root_id, folder)
        tree.item(root_id, open=True)
        # detect project root for building
        repo = find_repo_root(folder)
        state["repo"] = repo
        set_status(f"Folder: {folder}"
                   + ("" if repo else "  — no Makefile found (build disabled)"))

    # ----- editor (gutter + text) -----
    edit_area = tk.Frame(split, bg=BG)
    gutter = tk.Canvas(edit_area, width=52, bg=GUTTER_BG, highlightthickness=0)
    gutter.pack(side="left", fill="y")
    editor = tk.Text(edit_area, bg=BG, fg=FG, insertbackground="#aeafad",
                     selectbackground="#264f78", relief="flat", wrap="none",
                     font=mono, undo=True, tabs=("2c",), padx=8, pady=4)
    editor.pack(side="left", fill="both", expand=True)
    yscroll = tk.Scrollbar(edit_area, command=editor.yview)
    yscroll.pack(side="right", fill="y")
    split.add(edit_area, minsize=400)

    editor.tag_configure("cur", background=CUR_LINE)
    editor.tag_configure("kw", foreground=C_KW)
    editor.tag_configure("type", foreground=C_TYPE)
    editor.tag_configure("num", foreground=C_NUM)
    editor.tag_configure("pp", foreground=C_PP)
    editor.tag_configure("str", foreground=C_STRING)
    editor.tag_configure("com", foreground=C_COMMENT)

    # ---------------- output / live debug log ----------------
    LOG_MAX_LINES = 5000

    log_frame = tk.Frame(root, bg=PANEL, height=312)
    log_frame.pack(side="bottom", fill="x")
    log_frame.pack_propagate(False)

    src_var = tk.StringVar(value="Auto")
    port_var = tk.StringVar(value="")
    baud_var = tk.StringVar(value="115200")
    auto_var = tk.BooleanVar(value=True)    # open the log after every upload
    scroll_var = tk.BooleanVar(value=True)  # follow the tail
    stamp_var = tk.BooleanVar(value=True)   # prefix target lines with a clock
    port_desc = {}

    ctl = tk.Frame(log_frame, bg=PANEL)
    ctl.pack(side="top", fill="x", padx=8, pady=(5, 2))

    def ctl_text(text):
        tk.Label(ctl, text=text, bg=PANEL, fg="#9a9a9a",
                 font=UI(8)).pack(side="left", padx=(12, 4))

    def mkctl(text, cmd, bg="#3a3d41", padx=10):
        return tk.Button(ctl, text=text, command=cmd, bg=bg, fg="white",
                         activebackground="#1177bb", activeforeground="white",
                         relief="flat", padx=padx, pady=2, font=UI(9),
                         cursor="hand2", borderwidth=0)

    def mkchk(text, var, parent=None):
        return tk.Checkbutton(parent or ctl, text=text, variable=var, bg=PANEL, fg="#cccccc",
                              activebackground=PANEL, activeforeground="white",
                              selectcolor=BG, highlightthickness=0, bd=0,
                              font=UI(8), cursor="hand2")

    tk.Label(ctl, text="DEBUG LOG", bg=PANEL, fg="#cccccc",
             font=UI(8, "bold")).pack(side="left")

    ctl_text("source")
    src_box = ttk.Combobox(ctl, textvariable=src_var, width=12, state="readonly",
                           style="Dark.TCombobox",
                           values=("Auto", "Semihosting", "Serial"))
    src_box.pack(side="left")
    ctl_text("port")
    # Wide enough for "/dev/ttyUSB0"; "COM11" never needed the room.
    port_box = ttk.Combobox(ctl, textvariable=port_var, width=14, state="readonly",
                            style="Dark.TCombobox", values=())
    port_box.pack(side="left")
    mkctl("\u21bb", lambda: refresh_ports(), padx=6).pack(side="left", padx=(4, 0))
    ctl_text("baud")
    baud_box = ttk.Combobox(ctl, textvariable=baud_var, width=7, state="readonly",
                            style="Dark.TCombobox",
                            values=("9600", "19200", "38400", "57600", "115200"))
    baud_box.pack(side="left")

    monitor_btn = mkctl("Start Log", lambda: toggle_monitor())
    monitor_btn.pack(side="right", padx=(6, 0))
    mkctl("Clear", lambda: clear_log()).pack(side="right", padx=(6, 0))
    mkchk("auto-open after upload", auto_var).pack(side="right", padx=(6, 0))
    mkchk("timestamps", stamp_var).pack(side="right")
    mkchk("follow", scroll_var).pack(side="right")

    # ---- send bar (keyboard -> serial device, e.g. AT commands) ----
    inbar = tk.Frame(log_frame, bg=PANEL)
    inbar.pack(side="bottom", fill="x", padx=8, pady=(0, 6))

    send_var = tk.StringVar()
    eol_var = tk.StringVar(value="CRLF")
    keys_var = tk.BooleanVar(value=False)   # send each keystroke as it is typed
    echo_var = tk.BooleanVar(value=True)    # show what was sent in the log

    tk.Label(inbar, text="send", bg=PANEL, fg="#9a9a9a",
             font=UI(8)).pack(side="left", padx=(0, 5))
    send_entry = tk.Entry(inbar, textvariable=send_var, bg=BG, fg=FG,
                          insertbackground=FG, relief="flat", font=MONO(10))
    send_entry.pack(side="left", fill="x", expand=True, ipady=3)
    send_btn = tk.Button(inbar, text="Send", command=lambda: send_line(), bg=ACCENT,
                         fg="white", activebackground="#1177bb", activeforeground="white",
                         relief="flat", padx=12, pady=2, font=UI(9),
                         cursor="hand2", borderwidth=0)
    send_btn.pack(side="right", padx=(6, 0))
    eol_box = ttk.Combobox(inbar, textvariable=eol_var, width=6, state="readonly",
                           style="Dark.TCombobox", values=("CRLF", "CR", "LF", "none"))
    eol_box.pack(side="right", padx=(6, 0))
    tk.Label(inbar, text="line ending", bg=PANEL, fg="#9a9a9a",
             font=UI(8)).pack(side="right", padx=(10, 4))
    mkchk("echo", echo_var, inbar).pack(side="right")
    mkchk("raw keys", keys_var, inbar).pack(side="right")

    logwrap = tk.Frame(log_frame, bg=PANEL)
    logwrap.pack(side="top", fill="both", expand=True)
    # state stays "normal" so mouse selection, Ctrl+C and Ctrl+A work exactly as
    # in any text field; edits are blocked by the key bindings further down.
    logbox = tk.Text(logwrap, bg="#0c0c0c", fg="#cccccc", relief="flat",
                     font=MONO(9), height=10, wrap="word",
                     selectbackground="#264f78", exportselection=True)
    logbox.pack(side="left", fill="both", expand=True, padx=(8, 0), pady=(2, 4))
    lsb = tk.Scrollbar(logwrap, command=logbox.yview)
    lsb.pack(side="right", fill="y")
    logbox.configure(yscrollcommand=lsb.set)
    logbox.tag_configure("ok", foreground="#4ec9b0")
    logbox.tag_configure("err", foreground="#f48771")
    logbox.tag_configure("head", foreground="#569cd6")
    logbox.tag_configure("dim", foreground="#7f7f7f")
    logbox.tag_configure("sent", foreground="#9cdcfe")

    status = tk.Label(root, text="Ready", bg=ACCENT, fg="white", anchor="w",
                      font=UI(9), padx=8)
    status.pack(side="bottom", fill="x")

    def set_status(text, color=ACCENT):
        status.configure(text=text, bg=color)

    # Target output arrives in arbitrary chunks from monitor threads: queue it
    # and flush on a 40 ms tick so the log stays live without flooding Tk.
    log_pending = []
    ui_queue = []
    port_tick = {"n": 0}
    log_lock = threading.Lock()
    log_line_state = {"at_start": True}

    def post_ui(fn):
        """Queue `fn` to run on the Tk thread. Safe to call from any worker."""
        with log_lock:
            ui_queue.append(fn)

    def log_raw(text, tag=None, stamp=False):
        """Append text verbatim (no newline added). Safe from any thread."""
        if not text:
            return
        with log_lock:
            log_pending.append((text, tag, stamp))

    def log(text, tag=None):
        log_raw(text + "\n", tag)

    def stream_log(text, tag=None):
        """Live target output: timestamped, never waits for a full line."""
        log_raw(text, tag, stamp=True)

    def _insert_log(text, tag, stamp):
        text = clean_stream_text(text)
        if not text:
            return
        tags = (tag,) if tag else ()
        if not (stamp and stamp_var.get()):
            logbox.insert("end", text, tags)
            log_line_state["at_start"] = text.endswith("\n")
            return
        for part in text.splitlines(keepends=True):
            if log_line_state["at_start"] and part.strip():
                logbox.insert("end", time.strftime("[%H:%M:%S] "), ("dim",))
            logbox.insert("end", part, tags)
            log_line_state["at_start"] = part.endswith("\n")

    def drain_log():
        with log_lock:
            items = log_pending[:]
            del log_pending[:]
            actions = ui_queue[:]
            del ui_queue[:]
        if items:
            for text, tag, stamp in items:
                _insert_log(text, tag, stamp)
            total = int(logbox.index("end-1c").split(".")[0])
            if total > LOG_MAX_LINES:
                logbox.delete("1.0", f"{total - LOG_MAX_LINES}.0")
            if scroll_var.get():
                logbox.see("end")
        for fn in actions:
            try:
                fn()
            except Exception as e:
                log(f"UI ERROR: {e}", "err")
        port_tick["n"] += 1
        if port_tick["n"] >= 125:          # ~5 s: notice adapters plugged in later
            port_tick["n"] = 0
            rescan_ports_quietly()
        root.after(40, drain_log)

    def clear_log():
        with log_lock:
            del log_pending[:]
        log_line_state["at_start"] = True
        logbox.delete("1.0", "end")

    # ---------------- log: read-only, but fully selectable and copyable ------
    def copy_all_log(_=None):
        text = logbox.get("1.0", "end-1c")
        if text:
            root.clipboard_clear()
            root.clipboard_append(text)
            set_status(f"Copied the whole log ({len(text)} chars) to the clipboard.")
        return "break"

    def copy_log(_=None):
        try:
            text = logbox.get("sel.first", "sel.last")
        except tk.TclError:
            text = ""
        if not text:
            return copy_all_log()
        root.clipboard_clear()
        root.clipboard_append(text)
        set_status(f"Copied {len(text)} chars to the clipboard.")
        return "break"

    def select_all_log(_=None):
        logbox.tag_add("sel", "1.0", "end-1c")
        logbox.mark_set("insert", "1.0")
        return "break"

    def save_log(_=None):
        path = filedialog.asksaveasfilename(
            title="Save debug log", defaultextension=".log",
            initialfile="ht5023-debug.log",
            filetypes=[("Log files", "*.log *.txt"), ("All files", "*.*")])
        if not path:
            return "break"
        try:
            Path(path).write_text(logbox.get("1.0", "end-1c"), encoding="utf-8")
            set_status(f"Saved log to {path}")
        except Exception as e:
            messagebox.showerror("Save failed", str(e))
        return "break"

    def log_readonly(event):
        """Block edits without breaking selection, copy or navigation."""
        if (event.state & 0x0004) and event.keysym.lower() in ("c", "a", "insert"):
            return None
        if event.keysym in ("Left", "Right", "Up", "Down", "Prior", "Next", "Home",
                            "End", "Shift_L", "Shift_R", "Control_L", "Control_R"):
            return None
        return "break"

    log_menu = tk.Menu(root, tearoff=0, bg=PANEL, fg=FG, activebackground=ACCENT,
                       activeforeground="white", borderwidth=0)
    log_menu.add_command(label="Copy", command=copy_log)
    log_menu.add_command(label="Copy all", command=copy_all_log)
    log_menu.add_command(label="Select all", command=select_all_log)
    log_menu.add_separator()
    log_menu.add_command(label="Save to file…", command=save_log)
    log_menu.add_command(label="Clear", command=lambda: clear_log())

    def show_log_menu(event):
        try:
            log_menu.tk_popup(event.x_root, event.y_root)
        finally:
            log_menu.grab_release()

    logbox.bind("<Key>", log_readonly)
    logbox.bind("<<Paste>>", lambda e: "break")
    logbox.bind("<<Cut>>", lambda e: "break")
    logbox.bind("<Button-2>", lambda e: "break")
    logbox.bind("<Button-3>", show_log_menu)
    logbox.bind("<Control-a>", select_all_log)
    logbox.bind("<Control-A>", select_all_log)

    # ---------------- send bar behaviour ----------------
    EOL_BYTES = {"CRLF": b"\r\n", "CR": b"\r", "LF": b"\n", "none": b""}
    send_history = []
    history_pos = {"i": 0}

    def can_send():
        if not state["monitoring"]:
            return False, "start the debug log first (Start Log / F6)"
        handles = state["mon_handles"] or {}
        if handles.get("serial") or handles.get("telnet"):
            return True, ""
        if state.get("mon_source") == "serial":
            return False, "the serial port is not open yet"
        return False, "the semihosting channel is not open yet"

    def send_bytes(data, shown=None, retry=True):
        ok, why = can_send()
        if not ok:
            # Nothing open on this source: if an adapter exists, move there and
            # send once the port opens.
            if retry and state.get("mon_source") != "serial" and best_serial_port():
                switch_to_serial(lambda: send_bytes(data, shown, retry=False))
                return False
            log(f"Cannot send: {why}.", "err")
            if not best_serial_port() and not state["monitoring"]:
                log("       Start the debug log (F6). With only the J-Link attached, "
                    "use source Semihosting -- typing goes through the debug cable "
                    "to examples that read it (text_back).", "err")
            return False
        ok, err = serial_send(state["mon_handles"], data)
        if not ok:
            log(f"Send failed: {err}", "err")
            return False
        if shown is not None and echo_var.get():
            log_raw(f"> {shown}\n", "sent")
        return True

    def send_line(_=None):
        text = send_var.get()
        if send_bytes(text.encode("utf-8", "replace") + EOL_BYTES[eol_var.get()], text):
            if text and (not send_history or send_history[-1] != text):
                send_history.append(text)
            history_pos["i"] = len(send_history)
            send_var.set("")
        return "break"

    def send_key(event):
        """raw-keys mode: every keystroke goes out immediately (no Enter)."""
        if not keys_var.get():
            return None
        if event.keysym == "Return":
            send_bytes(EOL_BYTES[eol_var.get()] or b"\r", "")
            return "break"
        if event.keysym == "BackSpace":
            send_bytes(b"\x08")
            return "break"
        if event.char and event.char.isprintable():
            if send_bytes(event.char.encode("utf-8", "replace")) and echo_var.get():
                log_raw(event.char, "sent")
            return "break"
        return "break"

    def history_prev(_=None):
        if not send_history:
            return "break"
        history_pos["i"] = max(0, history_pos["i"] - 1)
        send_var.set(send_history[history_pos["i"]])
        send_entry.icursor("end")
        return "break"

    def history_next(_=None):
        if not send_history:
            return "break"
        history_pos["i"] = min(len(send_history), history_pos["i"] + 1)
        send_var.set("" if history_pos["i"] >= len(send_history)
                     else send_history[history_pos["i"]])
        send_entry.icursor("end")
        return "break"

    send_entry.bind("<Key>", send_key)
    send_entry.bind("<Return>", send_line)
    send_entry.bind("<Up>", history_prev)
    send_entry.bind("<Down>", history_next)

    # ---------------- editor behaviour ----------------
    def highlight(_=None):
        content = editor.get("1.0", "end-1c")
        for t in ("kw", "type", "num", "pp", "str", "com"):
            editor.tag_remove(t, "1.0", "end")

        def apply(pattern, tag, flags=0):
            for m in re.finditer(pattern, content, flags):
                editor.tag_add(tag, f"1.0+{m.start()}c", f"1.0+{m.end()}c")

        apply(KEYWORDS, "kw")
        apply(TYPES, "type")
        apply(r"\b\d+\.?\d*[uUlLfF]*\b|\b0[xX][0-9a-fA-F]+\b", "num")
        apply(r"^[ \t]*#.*$", "pp", re.MULTILINE)
        apply(r"\"(\\.|[^\"\\])*\"|'(\\.|[^'\\])*'", "str")
        apply(r"//[^\n]*|/\*.*?\*/", "com", re.DOTALL)
        for t in ("str", "com"):
            editor.tag_raise(t)
        update_gutter()

    def cur_line(_=None):
        editor.tag_remove("cur", "1.0", "end")
        editor.tag_add("cur", "insert linestart", "insert lineend+1c")

    def update_gutter(_=None):
        gutter.delete("all")
        i = editor.index("@0,0")
        last = int(editor.index("end-1c").split(".")[0])
        while True:
            d = editor.dlineinfo(i)
            if d is None:
                break
            ln = str(i).split(".")[0]
            gutter.create_text(46, d[1], anchor="ne", text=ln, fill=GUTTER_FG, font=mono)
            if int(ln) >= last:
                break
            i = editor.index(f"{i}+1line")

    def on_yview(*args):
        yscroll.set(*args)
        update_gutter()

    editor.configure(yscrollcommand=on_yview)

    def on_key(_=None):
        cur_line()
        if getattr(on_key, "_job", None):
            root.after_cancel(on_key._job)
        on_key._job = root.after(120, highlight)

    editor.bind("<KeyRelease>", on_key)
    editor.bind("<ButtonRelease-1>", cur_line)
    editor.bind("<MouseWheel>", lambda e: root.after(1, update_gutter))
    editor.bind("<Configure>", lambda e: update_gutter())

    # ---------------- file ops ----------------
    def load_path(path):
        try:
            text = Path(path).read_text(encoding="utf-8")
        except Exception as e:
            messagebox.showerror("Open failed", str(e))
            return
        editor.delete("1.0", "end")
        editor.insert("1.0", text)
        editor.edit_reset()
        rp = Path(path).resolve()
        state["path"] = str(rp)
        state["saved_text"] = text
        path_lbl.configure(text=state["path"])
        if find_repo_root(rp):
            state["repo"] = find_repo_root(rp)
        ex = example_from_path(path)
        if is_sketch(rp):
            # Sketches are built by arduino-cli, not the Makefile, so the
            # examples/<name>/ rule does not apply to them.
            folder = sketch_folder(rp)
            root.title(f"HT5023 Flasher — {rp.name}  [Arduino sketch]")
            note = ("" if folder else
                    f"  — a sketch must live in a folder named '{rp.stem}'")
        else:
            root.title(f"HT5023 Flasher — {rp.name}"
                       + (f"  [example: {ex}]" if ex else "  [not an example folder]"))
            note = ("" if ex else
                    "  — not under examples/<name>/ (build won't apply)")
        highlight()
        cur_line()
        set_status(f"Loaded {rp.name}" + note)

    def refresh_explorer(_=None):
        """Re-read the folder tree and the open file from disk.

        Keeps expanded folders, the selection and the scroll position, so new
        files (a freshly added example, say) appear without losing your place.
        """
        folder = state["workspace"]
        if not folder:
            set_status("Nothing to refresh — open a folder first.")
            return "break"

        opened = {info[0] for nid, info in node_info.items()
                  if info[1] and tree.item(nid, "open")}
        focused = (node_info.get(tree.focus()) or (None, False))[0]
        offset = tree.yview()[0]

        set_workspace(folder)

        def restore(node):
            for child in tree.get_children(node):
                info = node_info.get(child)
                if not info:
                    continue
                path, is_dir = info
                if is_dir and path in opened:
                    populate(child, path)
                    tree.item(child, open=True)
                    restore(child)
                if focused and path == focused:
                    tree.selection_set(child)
                    tree.focus(child)

        if state["tree_root"]:
            restore(state["tree_root"])
        try:
            tree.yview_moveto(offset)
        except Exception:
            pass

        # Pick up edits made to the open file outside the editor.
        path = state["path"]
        reloaded = ""
        if path and Path(path).exists():
            try:
                disk = Path(path).read_text(encoding="utf-8")
            except Exception:
                disk = None
            if disk is not None and disk != editor.get("1.0", "end-1c"):
                dirty = editor.get("1.0", "end-1c") != state.get("saved_text", "")
                if not dirty:
                    load_path(path)
                    reloaded = f", reloaded {Path(path).name}"
                elif messagebox.askyesno(
                        "Reload file",
                        f"{Path(path).name} changed on disk.\n\n"
                        "Discard your unsaved edits and reload it?"):
                    load_path(path)
                    reloaded = f", reloaded {Path(path).name}"
                else:
                    reloaded = f", kept your unsaved {Path(path).name}"

        set_status(f"Explorer refreshed{reloaded} — {folder}")
        return "break"

    def open_folder():
        folder = filedialog.askdirectory(
            title="Open project folder",
            initialdir=str(state["workspace"] or Path.home()))
        if folder:
            set_workspace(folder)

    def open_file():
        start = state["workspace"] or Path.home()
        path = filedialog.askopenfilename(
            initialdir=str(start), title="Open source file",
            filetypes=[("C/C++ source", "*.c *.h *.cpp"), ("All files", "*.*")])
        if path:
            load_path(path)

    def save_file():
        if not state["path"]:
            messagebox.showwarning("No file", "Open a file first.")
            return False
        try:
            content = editor.get("1.0", "end-1c")
            Path(state["path"]).write_text(content, encoding="utf-8")
            state["saved_text"] = content
            set_status(f"Saved {Path(state['path']).name}")
            return True
        except Exception as e:
            messagebox.showerror("Save failed", str(e))
            return False

    # ---------------- flash ----------------
    def upload_is_usb():
        return upload_var.get().startswith("USB")

    def on_upload_choice(_=None):
        flash_btn.configure(text="Compile & Upload (USB)" if upload_is_usb()
                            else "Compile & Upload (J-Link)")
        if upload_is_usb():
            set_status("USB upload: needs the bootloader installed (make bootloader) "
                       "and the adapter on PC11/PC12.")
        else:
            why = jlink_unavailable(state["tools"])
            if why:
                set_status(f"J-Link selected but {why}.", "#b03a2e")
    upload_box.bind("<<ComboboxSelected>>", on_upload_choice)

    # ---------------- Arduino actions ----------------
    def do_new_sketch():
        if state["busy"]:
            return
        base = state["workspace"] or (state["repo"] and str(state["repo"])) or os.getcwd()
        # Sketches belong together; default to the repo's sketches/ folder.
        if state["repo"] and (Path(state["repo"]) / "sketches").exists():
            base = str(Path(state["repo"]) / "sketches")
        name = simpledialog.askstring("New sketch", "Sketch name:",
                                      initialvalue="MySketch", parent=root)
        if not name:
            return
        try:
            path = new_sketch(base, name)
        except Exception as e:
            messagebox.showerror("Could not create the sketch", str(e))
            return
        refresh_explorer()
        load_path(path)
        log(f"Created {path}", "ok")
        log("Pick 'via: USB (bootloader)' and press Compile & Upload (F5).", "dim")
        set_status(f"New sketch: {path.name}")

    def do_libraries():
        if state["busy"]:
            return
        cli = find_arduino_cli()
        if cli is None:
            messagebox.showerror(
                "arduino-cli not found",
                "Install the Arduino IDE (it ships arduino-cli), or put "
                "arduino-cli on PATH.")
            return

        win = tk.Toplevel(root)
        win.title("Arduino libraries")
        win.configure(bg=PANEL)
        win.geometry("560x420")
        win.transient(root)

        tk.Label(win, text="Installed libraries", bg=PANEL, fg=FG,
                 font=UI(10, "bold")).pack(anchor="w", padx=12, pady=(12, 4))
        listbox = tk.Listbox(win, bg=BG, fg=FG, relief="flat",
                             font=MONO(10), selectbackground=ACCENT,
                             highlightthickness=0)
        listbox.pack(fill="both", expand=True, padx=12)

        row = tk.Frame(win, bg=PANEL)
        row.pack(fill="x", padx=12, pady=10)
        tk.Label(row, text="Install:", bg=PANEL, fg=FG,
                 font=UI(9)).pack(side="left")
        entry = tk.Entry(row, bg=BG, fg=FG, insertbackground=FG, relief="flat",
                         font=MONO(10))
        entry.pack(side="left", fill="x", expand=True, padx=8, ipady=3)
        entry.insert(0, "Adafruit GFX Library")

        status = tk.Label(win, text="", bg=PANEL, fg="#cccccc",
                          font=UI(9), anchor="w")
        status.pack(fill="x", padx=12, pady=(0, 10))

        env = make_env(state["tools"], state["repo"]) if state["repo"] \
            else os.environ.copy()

        def reload_list():
            status.configure(text="reading…")
            listbox.delete(0, "end")

            def worker():
                libs = arduino_libraries(cli, env)

                def show():
                    for name, version in libs:
                        listbox.insert("end", f"{name:<38} {version}")
                    status.configure(
                        text=f"{len(libs)} installed — they are shared with the "
                             "Arduino IDE")
                post_ui(show)
            threading.Thread(target=worker, daemon=True).start()

        def do_install():
            name = entry.get().strip()
            if not name:
                return
            status.configure(text=f"installing {name}…")
            btn.configure(state="disabled")

            def worker():
                ok = arduino_lib_install(cli, env, name, log)

                def done():
                    btn.configure(state="normal")
                    status.configure(text=("installed " + name) if ok
                                     else ("could not install " + name +
                                           " — see the debug log"))
                    if ok:
                        reload_list()
                post_ui(done)
            threading.Thread(target=worker, daemon=True).start()

        btn = tk.Button(row, text="Install", command=do_install, bg=ACCENT,
                        fg="white", relief="flat", padx=12, pady=3,
                        font=UI(9, "bold"), cursor="hand2",
                        borderwidth=0)
        btn.pack(side="left")
        entry.bind("<Return>", lambda e: do_install())
        reload_list()

    def do_install_bootloader():
        if state["busy"]:
            return
        if not messagebox.askyesno(
                "Install the UART bootloader",
                "This programs the bootloader at 0x0000 over SWD, so it needs "
                "the J-Link connected this once.\n\n"
                "Afterwards, sketches upload over the USB adapter alone.\n\n"
                "Anything currently on the board is erased. Continue?"):
            return
        was_monitoring = state["monitoring"]
        if was_monitoring:
            stop_monitor(wait=True)
        clear_log()
        set_busy(True)
        set_status("Installing the bootloader…", "#b58900")
        speed = (speed_var.get() or "1000").strip()
        repo, tools = state["repo"], state["tools"]

        def worker():
            ok = False
            try:
                ok = install_bootloader(tools, repo, speed, log)
            except Exception as e:
                log(f"UNEXPECTED ERROR: {e}", "err")

            def done():
                set_busy(False)
                set_status("✓ Bootloader installed — USB uploads are ready."
                           if ok else "✗ Bootloader install failed — see the log.",
                           "#2d8a3e" if ok else "#b03a2e")
                if ok:
                    upload_var.set("USB (bootloader)")
                    on_upload_choice()
            post_ui(done)
        threading.Thread(target=worker, daemon=True).start()

    def set_busy(busy):
        state["busy"] = busy
        s = "disabled" if busy else "normal"
        for b in (folder_btn, open_btn, save_btn, flash_btn, monitor_btn,
                  sketch_btn, lib_btn, boot_btn):
            b.configure(state=s)
        if busy:
            flash_btn.configure(text="Working…")
        else:
            on_upload_choice()

    def do_flash():
        if state["busy"]:
            return
        if not state["path"]:
            messagebox.showwarning("No file", "Open a source file first.")
            return
        # Say so before spending a build on it.
        if not upload_is_usb():
            why = jlink_unavailable(state["tools"])
            if why:
                clear_log()
                log(f"Cannot program over SWD: {why}.", "err")
                set_status("✗ No J-Link — switch 'via' to USB (bootloader).",
                           "#b03a2e")
                return
        if not save_file():
            return
        # Both links are exclusive: the J-Link serves one client, and a USB upload
        # needs the port the serial log is holding. Either way the log lets go
        # first and comes back afterwards.
        was_monitoring = state["monitoring"]
        if was_monitoring:
            # The flash resets the target anyway; skip the monitor's own reset.
            (state["mon_handles"] or {}).update({"skip_reset": True})
            stop_monitor(wait=True)
        clear_log()
        set_busy(True)
        set_status("Building & uploading…", "#b58900")
        speed = (speed_var.get() or "1000").strip()
        path, repo = state["path"], state["repo"]

        usb = upload_is_usb()
        port, baud = port_var.get(), baud_var.get()
        auto_reset = bool(autoreset_var.get())
        # Hold the sketch in the bootloader until the log is listening, so its
        # startup output is not lost. Only worth doing when a log will open.
        defer_start = bool(auto_var.get() or was_monitoring)

        def worker():
            if was_monitoring and repo is not None:
                wait_for_unlocked(repo / "build" / "app.elf")
            ok = False
            try:
                sketch = is_sketch(path)
                if not usb:
                    if sketch:
                        binary = run_arduino_pipeline(path, state["tools"], repo, log,
                                                      boot_app=False)
                        ok = bool(binary) and flash_bin_jlink(
                            binary, state["tools"], speed, repo, log,
                            tag=Path(path).stem)
                    else:
                        ok = run_pipeline(path, state["tools"], speed, repo, log,
                                          flash=True)
                else:
                    # Build for the bootloader layout, then send it over the wire.
                    if sketch:
                        binary = run_arduino_pipeline(path, state["tools"], repo, log,
                                                      boot_app=True)
                        ok = binary is not None
                    else:
                        binary = (repo / "build" / "app.bin") if repo else None
                        ok = run_pipeline(path, state["tools"], speed, repo, log,
                                          flash=False, boot_app=True)
                    if ok:
                        if not port:
                            log("Cannot upload: no serial port selected. Plug in the "
                                "USB-UART adapter and press ↻.", "err")
                            ok = False
                        else:
                            last = [-1]

                            def progress(done, total):
                                pct = done * 100 // max(1, total)
                                if pct != last[0]:
                                    last[0] = pct
                                    log(f"   {done}/{total} bytes  {pct}%")

                            err = usb_upload(port, baud, binary,
                                             log, progress=progress,
                                             do_reset=auto_reset,
                                             start_after=not defer_start)
                            if err:
                                log(f"USB UPLOAD FAILED: {err}", "err")
                                if "bootloader" in str(err).lower():
                                    log("If the board has never had the "
                                        "bootloader, press 'Install Bootloader "
                                        "(J-Link, once)' -- that is the only "
                                        "step the probe is needed for.", "err")
                                    log("Otherwise press the board's RESET "
                                        "button while the upload is listening, "
                                        "or tick 'adapter resets the board' if "
                                        "DTR/RTS is wired to RST.", "err")
                                ok = False
                            else:
                                log("UPLOAD OK -- application programmed over USB "
                                    "and started.", "ok")
            except Exception as e:
                log(f"UNEXPECTED ERROR: {e}", "err")

            def done():
                set_busy(False)
                set_status("✓ Upload complete — target running." if ok else
                           "✗ Failed — see the debug log below.",
                           "#2d8a3e" if ok else "#b03a2e")
                if ok and (auto_var.get() or was_monitoring):
                    root.after(400, start_monitor)
                    if usb and defer_start:
                        # The image is programmed but still parked: the
                        # bootloader is holding it. Start it only once the log
                        # has the port, otherwise everything the sketch prints
                        # in setup() -- usually the one banner it ever prints --
                        # is gone before anyone is listening.
                        def kick(n=0):
                            if (state["mon_handles"] or {}).get("serial"):
                                serial_send(state["mon_handles"], b"J\r\n")
                                log("Starting the sketch…", "dim")
                            elif state["monitoring"] and n < 40:
                                root.after(100, lambda: kick(n + 1))
                            else:
                                err = boot_start_app(port, baud, log)
                                log("Starting the sketch…" if not err else
                                    f"The sketch is programmed but did not "
                                    f"start: {err}", "dim" if not err else "err")
                        root.after(600, kick)
                elif ok and usb and defer_start:
                    # No log to wait for -- start it straight away.
                    boot_start_app(port, baud, log)
            post_ui(done)

        threading.Thread(target=worker, daemon=True).start()

    # ---------------- live debug log (serial / semihosting) ----------------
    def refresh_ports():
        ports = list_serial_ports()
        port_desc.clear()
        port_desc.update(dict(ports))
        names = [d for d, _ in ports]
        port_box.configure(values=names)
        if not names:
            port_var.set("")
        elif port_var.get() not in names:
            port_var.set(next((d for d, desc in ports if port_rank(desc, d) == 0),
                              next((d for d, desc in ports
                                    if is_likely_target_port(desc, d)), names[0])))
        chosen = port_var.get()
        if chosen:
            set_status(f"Serial port {chosen} — "
                       f"{port_kind(port_desc.get(chosen, ''), chosen)}"
                       f" ({port_desc.get(chosen, 'unknown')})")
        return ports

    def rescan_ports_quietly():
        """Hot-plug detection. Never overrides a selection the user can still use."""
        ports = list_serial_ports()
        names = [d for d, _ in ports]
        if names == list(port_box.cget("values")):
            return
        port_desc.clear()
        port_desc.update(dict(ports))
        port_box.configure(values=names)
        if port_var.get() not in names:
            port_var.set(next((d for d, desc in ports if port_rank(desc, d) == 0),
                              next((d for d, desc in ports
                                    if is_likely_target_port(desc, d)),
                                   names[0] if names else "")))

    def best_serial_port():
        ports = list_serial_ports()
        for device, desc in ports:          # a real USB-UART bridge first
            if port_rank(desc, device) == 0:
                return device
        for device, desc in ports:
            if is_likely_target_port(desc, device):
                return device
        return ""

    def resolve_source():
        """'Auto' uses the USB-TTL adapter when one is plugged in, else J-Link."""
        src = src_var.get()
        if src == "Serial":
            return "serial"
        if src == "Semihosting":
            return "semihost"
        port = port_var.get()
        if port:
            # Uploading over USB means that same cable carries the log, and
            # semihosting is not an option at all without a probe -- in either
            # case any port beats falling back to the J-Link.
            if (upload_is_usb() or is_likely_target_port(port_desc.get(port, ""), port)
                    or jlink_unavailable(state["tools"])):
                return "serial"
        return "semihost"

    def set_monitoring(m):
        state["monitoring"] = m
        monitor_btn.configure(text="Stop Log" if m else "Start Log",
                              bg="#a31515" if m else "#3a3d41")

    def stop_monitor(wait=False):
        if not state["monitoring"]:
            return
        if state["mon_stop"]:
            state["mon_stop"].set()
        handles = state["mon_handles"] or {}
        ser = handles.get("serial")
        if ser is not None:
            try:
                ser.cancel_read()
            except Exception:
                pass
        _kill_all([h for h in (handles.get("proc"), handles.get("srv")) if h])
        thread = state.get("mon_thread")
        if wait and thread is not None and thread.is_alive():
            thread.join(timeout=5)
            set_monitoring(False)

    def start_monitor():
        if state["monitoring"] or state["busy"]:
            return
        source = resolve_source()
        if source == "serial" and not port_var.get():
            refresh_ports()
        if source == "serial" and not port_var.get():
            log("ERROR: no serial port available — plug in the USB-TTL adapter "
                "wired to PC11 (TX2), press ↻, or set source to Semihosting.", "err")
            return

        stop_evt, handles = threading.Event(), {}
        state["mon_stop"], state["mon_handles"] = stop_evt, handles
        state["mon_source"] = source
        set_monitoring(True)
        speed = (speed_var.get() or "1000").strip()
        repo, port, baud = state["repo"], port_var.get(), baud_var.get()
        set_status("Debug log: " + (f"serial {port} @ {baud}" if source == "serial"
                                    else "semihosting over J-Link"), "#2d8a3e")

        def worker():
            try:
                if source == "serial":
                    run_serial_monitor(port, baud, log, stream_log, handles, stop_evt)
                else:
                    run_monitor(state["tools"], repo, speed, log, stream_log,
                                handles, stop_evt)
            except Exception as e:
                log(f"MONITOR ERROR: {e}", "err")

            def done():
                if state["mon_stop"] is stop_evt:
                    set_monitoring(False)
                    if not state["busy"]:
                        set_status("Debug log stopped.", ACCENT)
            post_ui(done)

        thread = threading.Thread(target=worker, daemon=True)
        state["mon_thread"] = thread
        thread.start()

    def switch_to_serial(then=None):
        """Move the debug log from semihosting to the serial adapter, then run `then`."""
        port = best_serial_port()
        if not port:
            return False
        rescan_ports_quietly()
        log(f"Sending needs the serial link -- switching the debug log to {port}.", "head")
        src_var.set("Serial")
        port_var.set(port)
        if state["monitoring"]:
            stop_monitor(wait=True)
        start_monitor()

        def wait_for_port(n=0):
            if (state["mon_handles"] or {}).get("serial"):
                if then:
                    then()
            elif state["monitoring"] and n < 50:
                root.after(100, lambda: wait_for_port(n + 1))
            else:
                log("Serial port did not open -- nothing was sent.", "err")
        root.after(200, wait_for_port)
        return True

    def toggle_monitor():
        if state["monitoring"]:
            set_status("Stopping debug log…", "#b58900")
            stop_monitor()
        else:
            start_monitor()

    root.bind("<Control-s>", lambda e: (save_file(), "break"))
    root.bind("<Control-o>", lambda e: (open_folder(), "break"))
    root.bind("<F5>", lambda e: do_flash())
    root.bind("<F6>", lambda e: toggle_monitor())
    root.bind("<Control-r>", refresh_explorer)
    root.bind("<Control-R>", refresh_explorer)
    root.bind("<Control-l>", lambda e: (clear_log(), "break"))
    root.protocol("WM_DELETE_WINDOW", lambda: (stop_monitor(), root.destroy()))

    # ---------------- initial state ----------------
    seed = find_repo_root(Path(__file__).resolve()) or Path.cwd()
    if find_repo_root(seed):
        set_workspace(seed)
        # Prefer an example that actually logs, so a fresh start shows output.
        for candidate in ("text_back", "blinky"):
            default = seed / "examples" / candidate / "main.c"
            if default.exists():
                load_path(default)
                break
    drain_log()
    ports = refresh_ports()
    log(f"Tools: gcc={tools['gcc_bin']}", "head")
    log(f"       make={tools['make']}")
    if tools["jlink"] is None:
        log("       jlink=not installed -- SWD programming and semihosting are "
            "unavailable; use 'via: USB (bootloader)'.", "dim")
    else:
        n = jlink_probes(tools)
        log(f"       jlink={tools['jlink']}"
            + ("  (no probe connected)" if n == 0 else ""))
    try:
        import serial  # noqa: F401
        if ports:
            log("Serial ports: "
                + ", ".join(f"{d} [{port_kind(desc, d)}] {desc}" for d, desc in ports))
            trouble = serial_permission_problem(ports)
            if trouble:
                log(trouble, "err")
        else:
            log("Serial ports: none detected — the debug log will use semihosting "
                "over the J-Link cable.", "dim")
    except Exception:
        log("pyserial is not installed, so the 'Serial' source is unavailable "
            "(pip install pyserial). Semihosting still works.", "dim")
    log("Open a folder in the sidebar, edit code, then click 'Compile & Upload' (F5).")
    log("C sources build with make; Arduino sketches (.ino) build with arduino-cli.")
    log("Arduino row: New Sketch, Libraries… (install any Arduino library), and "
        "Install Bootloader -- the one step that needs the J-Link. After that, "
        "'via: USB (bootloader)' programs the board over the USB cable alone.")
    log("The target's debug output streams below in real time once the upload "
        "finishes (F6 toggles it, Ctrl+L clears).")
    log("Type in the send box to talk to the device (Enter sends, Up/Down recalls "
        "history). Select any log text and Ctrl+C, or right-click for Copy all / "
        "Save to file.")
    log("Explorer ↻ Refresh (Ctrl+R) re-reads the folder and the open file — use it "
        "after adding files outside the app. ↻ next to 'port' rescans serial ports.")

    root.mainloop()


# --------------------------------------------------------------------------
def main():
    if "--selftest" in sys.argv:
        idx = sys.argv.index("--selftest")
        ex = sys.argv[idx + 1] if len(sys.argv) > idx + 1 else "blinky"
        repo = find_repo_root(Path(__file__).resolve()) or Path.cwd()
        src = repo / "examples" / ex / "main.c"
        tools = detect_tools()
        ok = run_pipeline(src, tools, "1000", repo, lambda t, tag=None: print(t), flash=False)
        print("SELFTEST:", "PASS" if ok else "FAIL")
        sys.exit(0 if ok else 1)
    main_gui()


if __name__ == "__main__":
    main()
