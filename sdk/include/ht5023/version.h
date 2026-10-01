#pragma once

#include <stdint.h>

#define HT5023_SDK_VERSION_MAJOR 1
#define HT5023_SDK_VERSION_MINOR 0
#define HT5023_SDK_VERSION_PATCH 0
#define HT5023_SDK_VERSION_STR   "1.0.0"

/* Encode a version triple into a single uint32 for compile-time comparison. */
#define HT5023_SDK_VERSION_ENCODE(maj, min, patch) \
    (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | ((uint32_t)(patch)))

#define HT5023_SDK_VERSION \
    HT5023_SDK_VERSION_ENCODE(HT5023_SDK_VERSION_MAJOR, \
                               HT5023_SDK_VERSION_MINOR, \
                               HT5023_SDK_VERSION_PATCH)

/* Evaluates to 1 at compile time if the SDK version is at least (maj, min, patch). */
#define HT5023_SDK_AT_LEAST(maj, min, patch) \
    (HT5023_SDK_VERSION >= HT5023_SDK_VERSION_ENCODE((maj), (min), (patch)))
