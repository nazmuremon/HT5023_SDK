#include "Stream.h"

#include "Arduino.h"

#define PARSE_TIMEOUT 1000u  // ms between valid characters while parsing

void Stream::setTimeout(unsigned long timeout_ms) { timeout_ = timeout_ms; }

unsigned long Stream::getTimeout(void) const { return timeout_; }

int Stream::timedRead() {
  startMillis_ = millis();
  do {
    const int c = read();
    if (c >= 0) return c;
  } while (millis() - startMillis_ < timeout_);
  return -1;
}

int Stream::timedPeek() {
  startMillis_ = millis();
  do {
    const int c = peek();
    if (c >= 0) return c;
  } while (millis() - startMillis_ < timeout_);
  return -1;
}

// Returns the next digit without consuming it, dropping whatever the lookahead
// mode says to drop. -1 on timeout.
int Stream::peekNextDigit(LookaheadMode lookahead, bool detectDecimal) {
  for (;;) {
    const int c = timedPeek();
    if (c < 0 || c == '-' || (c >= '0' && c <= '9') ||
        (detectDecimal && c == '.')) {
      return c;
    }
    switch (lookahead) {
      case SKIP_NONE:
        return -1;
      case SKIP_WHITESPACE:
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return -1;
        break;
      case SKIP_ALL:
        break;
    }
    read();  // discard and try again
  }
}

// -------------------------------------------------------------- find ------

bool Stream::find(const char* target) {
  return findUntil(target, strlen(target), nullptr, 0u);
}

bool Stream::find(const char* target, size_t length) {
  return findUntil(target, length, nullptr, 0u);
}

bool Stream::findUntil(const char* target, const char* terminator) {
  return findUntil(target, strlen(target), terminator,
                   terminator ? strlen(terminator) : 0u);
}

bool Stream::findUntil(const char* target, size_t targetLen,
                       const char* terminator, size_t termLen) {
  if (!target || targetLen == 0u) return true;

  size_t index = 0;      // how much of target has matched so far
  size_t termIndex = 0;  // ... and of terminator

  for (;;) {
    const int c = timedRead();
    if (c < 0) return false;

    // Restarting from 0 on a mismatch is not a full KMP, but a partial match
    // that is itself a prefix of the target is rare in the kind of framing this
    // is used for ("OK", "\r\n>"), and the simple version cannot run away.
    index = (c == target[index]) ? index + 1u : (c == target[0] ? 1u : 0u);
    if (index >= targetLen) return true;

    if (termLen > 0u) {
      termIndex = (c == terminator[termIndex]) ? termIndex + 1u
                                               : (c == terminator[0] ? 1u : 0u);
      if (termIndex >= termLen) return false;
    }
  }
}

// -------------------------------------------------------------- parse -----

long Stream::parseInt(LookaheadMode lookahead, char ignore) {
  bool isNegative = false;
  long value = 0;
  int c;

  c = peekNextDigit(lookahead, false);
  if (c < 0) return 0;

  do {
    if (c == ignore) {
      // skip
    } else if (c == '-') {
      isNegative = true;
    } else if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
    } else {
      break;
    }
    read();
    c = timedPeek();
  } while ((c >= '0' && c <= '9') || c == ignore);

  return isNegative ? -value : value;
}

float Stream::parseFloat(LookaheadMode lookahead, char ignore) {
  bool isNegative = false;
  bool isFraction = false;
  double value = 0.0;
  double fraction = 1.0;
  int c;

  c = peekNextDigit(lookahead, true);
  if (c < 0) return 0.0f;

  do {
    if (c == ignore) {
      // skip
    } else if (c == '-') {
      isNegative = true;
    } else if (c == '.') {
      isFraction = true;
    } else if (c >= '0' && c <= '9') {
      value = value * 10.0 + (c - '0');
      if (isFraction) fraction *= 0.1;
    } else {
      break;
    }
    read();
    c = timedPeek();
  } while ((c >= '0' && c <= '9') || (c == '.' && !isFraction) || c == ignore);

  if (isFraction) value *= fraction;
  return (float)(isNegative ? -value : value);
}

// -------------------------------------------------------------- read ------

size_t Stream::readBytes(char* buffer, size_t length) {
  if (!buffer) return 0;
  size_t count = 0;
  while (count < length) {
    const int c = timedRead();
    if (c < 0) break;
    *buffer++ = (char)c;
    count++;
  }
  return count;
}

size_t Stream::readBytesUntil(char terminator, char* buffer, size_t length) {
  if (!buffer || length < 1u) return 0;
  size_t index = 0;
  while (index < length) {
    const int c = timedRead();
    if (c < 0 || (char)c == terminator) break;
    *buffer++ = (char)c;
    index++;
  }
  return index;  // the terminator is consumed but not stored
}

String Stream::readString() {
  String ret;
  int c = timedRead();
  while (c >= 0) {
    ret += (char)c;
    c = timedRead();
  }
  return ret;
}

String Stream::readStringUntil(char terminator) {
  String ret;
  int c = timedRead();
  while (c >= 0 && (char)c != terminator) {
    ret += (char)c;
    c = timedRead();
  }
  return ret;
}
