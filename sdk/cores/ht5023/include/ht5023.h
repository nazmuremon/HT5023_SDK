#pragma once

/* CMSIS-style device header wrapper.
 *
 * Named in lower case on purpose: a case-sensitive filesystem (Linux) has no
 * second chance at "HT5023.h", while a case-insensitive one (Windows, default
 * macOS) resolves either spelling to this file. Keeping both spellings as real
 * files is impossible on Windows, where they collide. */
#include "ht5023/core/device.h"
