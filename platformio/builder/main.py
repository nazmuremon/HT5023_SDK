"""PlatformIO build script for HT5023 (Cortex-M0).

Produces firmware.elf/.bin with arm-none-eabi-gcc and uploads either over SWD
through a SEGGER J-Link or over USB-UART through the resident bootloader.
"""

import sys
from os.path import isfile, join, normpath

from SCons.Script import AlwaysBuild, COMMAND_LINE_TARGETS, DefaultEnvironment, SConscript

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

# SCons runs this through exec(), so there is no __file__ to hang a path off --
# the platform directory is the reliable anchor.
sys.path.insert(0, join(platform.get_dir(), "builder"))
from framework_dir import find_framework_dir  # noqa: E402

framework_dir = find_framework_dir(platform.get_dir())
variant_dir = join(framework_dir, "variants", board.get("build.variant", "ht5023_blinky"))
uploader = join(framework_dir, "tools", "ht5023_upload.py")

# Which upload path is in play decides where the image has to be linked, so it
# has to be known before the linker script is chosen.
upload_protocol = env.subst("$UPLOAD_PROTOCOL") or board.get("upload.protocol", "jlink")
BOOTLOADER_PROTOCOLS = ("bootloader", "usb")

# The bootloader owns 0x0000-0x1FFF, so an image uploaded through it must be
# linked to run from 0x2000. Picking the script from the protocol keeps the two
# in step -- the wrong one produces a board that programs fine and never runs.
default_ldscript = "ht5023_app.ld" if upload_protocol in BOOTLOADER_PROTOCOLS else "ht5023.ld"
ldscript = join(variant_dir, board.get("build.ldscript", default_ldscript))
if not isfile(ldscript):
    sys.stderr.write("Linker script not found: %s\n" % ldscript)
    env.Exit(1)

env.Replace(
    AR="arm-none-eabi-gcc-ar",
    AS="arm-none-eabi-gcc",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-gcc-ranlib",
    SIZETOOL="arm-none-eabi-size",
    PROGNAME="firmware",
    PROGSUFFIX=".elf",
)

# Standard PlatformIO size reporting: these are what draw the "RAM: [== ] 6.9%"
# bars and enforce the board's maximum_size / maximum_ram_size. The previous
# script replaced SIZEPRINTCMD with a bare `size -A`, which printed a section
# table and silently skipped both checks.
env.Replace(
    SIZEPROGREGEXP=r"^(?:\.isr_vector|\.text|\.rodata|\.data|\.init_array|\.fini_array|\.ARM\.exidx)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.ramfunc|\.noinit)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",
)

env.Append(
    ASFLAGS=["-mcpu=cortex-m0", "-mthumb", "-x", "assembler-with-cpp"],
    CFLAGS=["-std=gnu11"],
    CCFLAGS=[
        "-mcpu=cortex-m0",
        "-mthumb",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-builtin",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-missing-field-initializers",
    ],
    CXXFLAGS=[
        "-std=gnu++17",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-threadsafe-statics",
    ],
    CPPDEFINES=[
        ("F_CPU", board.get("build.f_cpu", "9196000L")),   # Fsys, measured; see Arduino.h
        ("ARDUINO", 10819),
        "ARDUINO_ARCH_HT5023",
        "ARDUINO_%s" % board.get("build.mcu", "HT5023").upper(),
    ],
    LINKFLAGS=[
        "-mcpu=cortex-m0",
        "-mthumb",
        "-nostartfiles",
        "-Wl,--gc-sections",
        "-Wl,-u,Reset_Handler",
        "-Wl,-e,Reset_Handler",
        "-Wl,-u,_init",
        "-Wl,-u,_fini",
        "-Wl,-u,_sbrk",
        "-Wl,-u,_write",
        "-Wl,-u,_read",
        "-Wl,-u,_close",
        "-Wl,-u,_lseek",
        "-Wl,-u,_fstat",
        "-Wl,-u,_isatty",
        "-Wl,-u,_exit",
        "-Wl,-u,_kill",
        "-Wl,-u,_getpid",
        "-Wl,-Map,%s" % join("$BUILD_DIR", "${PROGNAME}.map"),
    ],
    LIBS=["m"],
    LDSCRIPT_PATH=ldscript,
)

extra_flags = board.get("build.extra_flags", "")
if extra_flags:
    env.ProcessFlags(extra_flags)

if "arduino" not in env.get("PIOFRAMEWORK", []):
    sys.stderr.write(
        "HT5023 needs `framework = arduino` in platformio.ini -- it is what "
        "provides Arduino.h, and with it the library ecosystem.\n"
    )
    env.Exit(1)

SConscript(join(platform.get_dir(), "builder", "frameworks", "arduino.py"), exports="env")

if "nobuild" in COMMAND_LINE_TARGETS:
    target_elf = join("$BUILD_DIR", "${PROGNAME}.elf")
    target_bin = join("$BUILD_DIR", "${PROGNAME}.bin")
else:
    target_elf = env.BuildProgram()
    target_bin = env.Command(
        join("$BUILD_DIR", "${PROGNAME}.bin"),
        target_elf,
        env.VerboseAction(
            "$OBJCOPY -O binary --gap-fill 0xFF $SOURCE $TARGET", "Building $TARGET"
        ),
    )
    env.Depends(target_bin, target_elf)

AlwaysBuild(env.Alias("nobuild", target_bin))
env.Alias("buildprog", target_bin, target_bin)

env.AddPlatformTarget(
    "size", target_elf, env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"),
    "Program Size",
)

# --------------------------------------------------------------------------
# Upload
#
# Both paths go through tools/ht5023_upload.py rather than the shell helper the
# old script called: `bash run_flash.sh` needs Git Bash to work on Windows,
# while $PYTHONEXE is the interpreter PlatformIO is already running under.
#
# Built as a command STRING, not a Python action function -- SCons reads a
# function action's return value as the exit status, so returning the command
# text made every upload look like a failure.
# --------------------------------------------------------------------------
_cmd = ['"$PYTHONEXE"', '"%s"' % uploader, "--bin", '"$SOURCE"']

if upload_protocol in BOOTLOADER_PROTOCOLS:
    _cmd += ["--method", "usb",
             "--baud", str(board.get("upload.speed", 115200)),
             "--port", '"$UPLOAD_PORT"']
    _actions = [
        env.VerboseAction(env.AutodetectUploadPort, "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE over the UART bootloader"),
    ]
else:
    _cmd += ["--method", "jlink", "--speed", str(board.get("upload.speed", 1000))]
    _actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE over SWD (J-Link)")]

env.Replace(UPLOADCMD=" ".join(_cmd))
env.AddPlatformTarget("upload", target_bin, _actions, "Upload")
