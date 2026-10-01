"""Locate the HT5023 Arduino core.

There is exactly one copy of the core in this repo --
`arduino/hardware/ht5023/ht5023` -- and every consumer (the Arduino IDE, the
Flasher GUI, PlatformIO) builds from it. An earlier version of this platform
carried its own copy under `platformio/framework-arduinoht5023`, which drifted:
it ended up declaring 8 KiB of RAM against the part's real 32 KiB and lost
several headers. Resolving the path in one place, used by every builder script,
is what stops that happening again.
"""

from os import environ
from os.path import abspath, isdir, isfile, join


# Relative to the platform directory (the folder holding platform.json).
_CANDIDATES = (
    # Normal layout: the platform lives inside the SDK checkout.
    ("..", "arduino", "hardware", "ht5023", "ht5023"),
    # Platform copied one level deeper, e.g. into a project's own tree.
    ("..", "..", "arduino", "hardware", "ht5023", "ht5023"),
    # A vendored copy beside platform.json, if anyone ships one.
    ("framework-arduinoht5023",),
)


def _looks_like_core(path):
    """A directory is the core only if the pieces the build needs are there."""
    return (
        isdir(join(path, "cores", "ht5023"))
        and isdir(join(path, "variants"))
        and isfile(join(path, "platform.txt"))
    )


def find_framework_dir(platform_dir):
    """Absolute path of the HT5023 Arduino core, or raise with the search list."""
    override = environ.get("HT5023_CORE_DIR")
    if override:
        override = abspath(override)
        if not _looks_like_core(override):
            raise RuntimeError(
                "HT5023_CORE_DIR=%s does not look like the HT5023 Arduino core "
                "(expected cores/ht5023, variants/ and platform.txt inside)."
                % override
            )
        return override

    tried = []
    for parts in _CANDIDATES:
        path = abspath(join(platform_dir, *parts))
        tried.append(path)
        if _looks_like_core(path):
            return path

    raise RuntimeError(
        "Could not find the HT5023 Arduino core. Looked in:\n  %s\n"
        "Set HT5023_CORE_DIR to the folder containing cores/ht5023 and "
        "variants/ (normally <sdk>/arduino/hardware/ht5023/ht5023)."
        % "\n  ".join(tried)
    )
