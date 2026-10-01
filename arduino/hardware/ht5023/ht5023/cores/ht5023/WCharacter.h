// WCharacter.h -- Arduino's character classification wrappers.
//
// These are the names sketches and libraries use (isDigit, isAlpha, ...) around
// the C library's is*() functions.
#pragma once

#include <ctype.h>

inline bool isAlphaNumeric(int c) { return isalnum(c) != 0; }
inline bool isAlpha(int c) { return isalpha(c) != 0; }
inline bool isAscii(int c) { return (c & ~0x7F) == 0; }
inline bool isWhitespace(int c) { return isblank(c) != 0; }
inline bool isControl(int c) { return iscntrl(c) != 0; }
inline bool isDigit(int c) { return isdigit(c) != 0; }
inline bool isGraph(int c) { return isgraph(c) != 0; }
inline bool isLowerCase(int c) { return islower(c) != 0; }
inline bool isPrintable(int c) { return isprint(c) != 0; }
inline bool isPunct(int c) { return ispunct(c) != 0; }
inline bool isSpace(int c) { return isspace(c) != 0; }
inline bool isUpperCase(int c) { return isupper(c) != 0; }
inline bool isHexadecimalDigit(int c) { return isxdigit(c) != 0; }
inline int toAscii(int c) { return c & 0x7F; }
inline int toLowerCase(int c) { return tolower(c); }
inline int toUpperCase(int c) { return toupper(c); }
