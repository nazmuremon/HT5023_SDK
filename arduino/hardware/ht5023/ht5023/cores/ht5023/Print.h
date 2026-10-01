// Print.h -- the Arduino Print interface.
//
// A subclass implements write(uint8_t) and gets the whole print()/println()
// family for free.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Printable.h"

class String;
class __FlashStringHelper;

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
 public:
  Print() : write_error_(0) {}
  virtual ~Print() = default;

  int getWriteError() { return write_error_; }
  void clearWriteError() { setWriteError(0); }

  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size);
  virtual int availableForWrite() { return 0; }

  size_t write(const char* str);
  size_t write(const char* buffer, size_t size) {
    return write((const uint8_t*)buffer, size);
  }
  size_t write(unsigned long value) { return write((uint8_t)value); }
  size_t write(long value) { return write((uint8_t)value); }
  size_t write(unsigned int value) { return write((uint8_t)value); }
  size_t write(int value) { return write((uint8_t)value); }

  size_t print(const char* str);
  size_t print(const String& str);
  size_t print(const __FlashStringHelper* str);
  size_t print(char c);
  size_t print(unsigned char value, int base = 10);
  size_t print(int value, int base = 10);
  size_t print(unsigned int value, int base = 10);
  size_t print(long value, int base = 10);
  size_t print(unsigned long value, int base = 10);
  size_t print(double value, int digits = 2);
  size_t print(const Printable& obj);

  size_t println(void);
  size_t println(const char* str);
  size_t println(const String& str);
  size_t println(const __FlashStringHelper* str);
  size_t println(char c);
  size_t println(unsigned char value, int base = 10);
  size_t println(int value, int base = 10);
  size_t println(unsigned int value, int base = 10);
  size_t println(long value, int base = 10);
  size_t println(unsigned long value, int base = 10);
  size_t println(double value, int digits = 2);
  size_t println(const Printable& obj);

  virtual void flush() {}

 protected:
  void setWriteError(int err = 1) { write_error_ = err; }
  size_t printNumber(unsigned long value, uint8_t base);
  size_t printSigned(long value, uint8_t base);

 private:
  int write_error_;
};
