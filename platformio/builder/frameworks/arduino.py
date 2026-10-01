"""Build the HT5023 Arduino core for PlatformIO.

The core is what makes the global library ecosystem usable on this part: a
library from the PlatformIO registry compiles here because it finds Arduino.h,
Print/Stream, Wire and SPI exactly where it expects them.
"""

import sys
from os.path import isdir, join, splitext

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

# No __file__ under SCons' exec(); anchor on the platform directory instead.
sys.path.insert(0, join(platform.get_dir(), "builder"))
from framework_dir import find_framework_dir  # noqa: E402

framework_dir = find_framework_dir(platform.get_dir())
core_dir = join(framework_dir, "cores", board.get("build.core", "ht5023"))
variant_dir = join(framework_dir, "variants", board.get("build.variant", "ht5023_blinky"))
libraries_dir = join(framework_dir, "libraries")

env.AppendUnique(
    CPPPATH=[core_dir, variant_dir],
    LIBSOURCE_DIRS=[libraries_dir],
)

# Some Arduino libraries include <avr/pgmspace.h> rather than <pgmspace.h>.
# The core ships both spellings; this puts the avr/ shim on the include path.
if isdir(join(core_dir, "avr")):
    env.AppendUnique(CPPPATH=[core_dir])

source_exts = ["c", "cc", "cpp", "cxx", "c++", "S", "spp", "SPP", "sx", "s", "asm", "ASM"]


def build_archive(name, src_dir):
    build_dir = join("$BUILD_DIR", name)
    objects = []
    for relpath in env.MatchSourceFiles(src_dir, src_exts=source_exts):
        object_base = splitext(relpath)[0]
        objects.append(env.Object(join(build_dir, object_base), join(src_dir, relpath)))
    return env.StaticLibrary(join("$BUILD_DIR", name), objects)


core_lib = build_archive("FrameworkArduinoCore", core_dir)
variant_lib = build_archive("FrameworkArduinoVariant", variant_dir)

# Variant first: it defines the pin tables the core's weak defaults fall back to.
env.Prepend(LIBS=[variant_lib, core_lib])
