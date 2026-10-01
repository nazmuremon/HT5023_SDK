#include "Print.h"
#include "WString.h"

#include <stdio.h>
#include <string.h>

size_t Print::write(const uint8_t* buffer, size_t size) {
  size_t written = 0;
  for (size_t i = 0; i < size; ++i) {
    written += write(buffer[i]);
  }
  return written;
}

size_t Print::write(const char* str) {
  if (!str) return 0;
  return write((const uint8_t*)str, strlen(str));
}

size_t Print::print(const char* str) {
  return write(str);
}

size_t Print::print(const String& str) {
  return write(str.c_str());
}

size_t Print::print(char c) {
  return write((uint8_t)c);
}

size_t Print::print(unsigned char value, int base) {
  return printNumber(value, (uint8_t)base);
}

size_t Print::print(int value, int base) {
  return printSigned(value, (uint8_t)base);
}

size_t Print::print(unsigned int value, int base) {
  return printNumber(value, (uint8_t)base);
}

size_t Print::print(long value, int base) {
  return printSigned(value, (uint8_t)base);
}

size_t Print::print(unsigned long value, int base) {
  return printNumber(value, (uint8_t)base);
}

size_t Print::print(double value, int digits) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
  return write(buffer);
}

size_t Print::print(const __FlashStringHelper* str) {
  // F() keeps the literal in ordinary flash on this part, so the pointer is
  // already a normal one -- nothing to copy out of a separate address space.
  return write(reinterpret_cast<const char*>(str));
}

size_t Print::print(const Printable& obj) {
  return obj.printTo(*this);
}

size_t Print::println(void) {
  return write((const uint8_t*)"\r\n", 2u);
}

size_t Print::println(const char* str) {
  return print(str) + println();
}

size_t Print::println(const String& str) {
  return print(str) + println();
}

size_t Print::println(char c) {
  return print(c) + println();
}

size_t Print::println(unsigned char value, int base) {
  return print(value, base) + println();
}

size_t Print::println(int value, int base) {
  return print(value, base) + println();
}

size_t Print::println(unsigned int value, int base) {
  return print(value, base) + println();
}

size_t Print::println(long value, int base) {
  return print(value, base) + println();
}

size_t Print::println(unsigned long value, int base) {
  return print(value, base) + println();
}

size_t Print::println(double value, int digits) {
  return print(value, digits) + println();
}

size_t Print::println(const __FlashStringHelper* str) {
  return print(str) + println();
}

size_t Print::println(const Printable& obj) {
  return print(obj) + println();
}

size_t Print::printNumber(unsigned long value, uint8_t base) {
  if (base < 2u) base = 10u;

  char buffer[33];
  char* ptr = &buffer[sizeof(buffer) - 1];
  *ptr = '\0';

  do {
    const unsigned long digit = value % base;
    *--ptr = (char)(digit < 10u ? ('0' + digit) : ('A' + digit - 10u));
    value /= base;
  } while (value != 0u);

  return write(ptr);
}

size_t Print::printSigned(long value, uint8_t base) {
  if (base == 10u && value < 0) {
    return write((uint8_t)'-') + printNumber((unsigned long)(-value), base);
  }
  return printNumber((unsigned long)value, base);
}
