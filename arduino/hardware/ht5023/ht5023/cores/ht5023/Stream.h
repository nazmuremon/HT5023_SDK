// Stream.h -- the Arduino Stream interface.
//
// Everything that talks to a peripheral over a byte pipe (Serial, Wire, and
// most sensor libraries' own transports) derives from this, and the parsing
// helpers below are what those libraries call to pull numbers and lines out of
// a reply. All of them are bounded by setTimeout(), 1 s by default.
#pragma once

#include "Print.h"
#include "WString.h"

// Passed to parseInt/parseFloat to skip a known separator.
enum LookaheadMode {
  SKIP_ALL,       // skip anything that is not part of the number
  SKIP_NONE,      // stop at the first character that cannot start a number
  SKIP_WHITESPACE // skip only spaces and tabs
};

#define NO_IGNORE_CHAR '\x01'

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}

  void setTimeout(unsigned long timeout_ms);
  unsigned long getTimeout(void) const;

  bool find(const char* target);
  bool find(const char* target, size_t length);
  bool find(char target) { return find(&target, 1u); }
  bool findUntil(const char* target, const char* terminator);
  bool findUntil(const char* target, size_t targetLen,
                 const char* terminate, size_t termLen);

  long parseInt(LookaheadMode lookahead = SKIP_ALL,
                char ignore = NO_IGNORE_CHAR);
  float parseFloat(LookaheadMode lookahead = SKIP_ALL,
                   char ignore = NO_IGNORE_CHAR);

  size_t readBytes(char* buffer, size_t length);
  size_t readBytes(uint8_t* buffer, size_t length) {
    return readBytes((char*)buffer, length);
  }
  size_t readBytesUntil(char terminator, char* buffer, size_t length);
  size_t readBytesUntil(char terminator, uint8_t* buffer, size_t length) {
    return readBytesUntil(terminator, (char*)buffer, length);
  }

  String readString();
  String readStringUntil(char terminator);

 protected:
  int timedRead();   // read with a timeout; -1 if nothing arrived
  int timedPeek();   // peek with a timeout; -1 if nothing arrived
  int peekNextDigit(LookaheadMode lookahead, bool detectDecimal);

 private:
  unsigned long timeout_ = 1000u;
  unsigned long startMillis_ = 0u;
};
