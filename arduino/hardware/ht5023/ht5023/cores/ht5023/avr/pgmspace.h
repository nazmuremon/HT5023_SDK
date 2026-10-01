// avr/pgmspace.h -- AVR's flash-access API, as plain memory access.
//
// The HT5023 addresses flash and RAM in one space, so "reading from program
// memory" is an ordinary load and PROGMEM is a no-op. The names exist because
// libraries written for the AVR use them unconditionally: every Adafruit
// driver's font and bitmap tables, OneWire's CRC table, and so on.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PROGMEM
#define PGM_P const char*
#define PGM_VOID_P const void*
#define PSTR(str) (str)
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))

typedef const char prog_char;
typedef const unsigned char prog_uchar;
typedef const int8_t prog_int8_t;
typedef const uint8_t prog_uint8_t;
typedef const int16_t prog_int16_t;
typedef const uint16_t prog_uint16_t;
typedef const int32_t prog_int32_t;
typedef const uint32_t prog_uint32_t;

#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))
#define pgm_read_ptr(addr) (*(void* const*)(addr))

#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word_near(addr) pgm_read_word(addr)
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#define pgm_read_float_near(addr) pgm_read_float(addr)
#define pgm_read_ptr_near(addr) pgm_read_ptr(addr)

#define pgm_read_byte_far(addr) pgm_read_byte(addr)
#define pgm_read_word_far(addr) pgm_read_word(addr)
#define pgm_read_dword_far(addr) pgm_read_dword(addr)
#define pgm_read_float_far(addr) pgm_read_float(addr)
#define pgm_read_ptr_far(addr) pgm_read_ptr(addr)

#define memcpy_P memcpy
#define memcmp_P memcmp
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcat_P strcat
#define strncat_P strncat
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strncasecmp_P strncasecmp
#define strlen_P strlen
#define strnlen_P strnlen
#define strstr_P strstr
#define strchr_P strchr
#define sprintf_P sprintf
#define snprintf_P snprintf
#define vsnprintf_P vsnprintf
#define printf_P printf
