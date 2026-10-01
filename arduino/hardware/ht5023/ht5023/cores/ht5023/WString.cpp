#include "WString.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// newlib has itoa/utoa but not the long forms; the core supplies those in
// wiring_extra.cpp. Declared here so this file does not have to pull in all of
// Arduino.h, which includes this header back.
extern "C" char* ltoa(long value, char* s, int base);
extern "C" char* ultoa(unsigned long value, char* s, int base);

// F() is a plain pointer on this part -- there is no separate flash address
// space to work around, so the __FlashStringHelper overloads just cast back.
static inline const char* flash_str(const __FlashStringHelper* f) {
  return reinterpret_cast<const char*>(f);
}

void String::init(void) {
  buffer_ = nullptr;
  capacity_ = 0u;
  length_ = 0u;
}

void String::invalidate(void) {
  free(buffer_);
  init();
}

bool String::changeBuffer(unsigned int maxStrLen) {
  char* newbuffer = (char*)realloc(buffer_, maxStrLen + 1u);
  if (!newbuffer) return false;
  buffer_ = newbuffer;
  capacity_ = maxStrLen;
  return true;
}

bool String::reserve(unsigned int size) {
  if (buffer_ && capacity_ >= size) return true;
  if (!changeBuffer(size)) return false;
  if (length_ == 0u) buffer_[0] = '\0';
  return true;
}

String& String::copy(const char* cstr, unsigned int length) {
  if (!reserve(length)) {
    invalidate();
    return *this;
  }
  length_ = length;
  memcpy(buffer_, cstr, length);
  buffer_[length] = '\0';
  return *this;
}

void String::move(String& rhs) {
  free(buffer_);
  buffer_ = rhs.buffer_;
  capacity_ = rhs.capacity_;
  length_ = rhs.length_;
  rhs.init();
}

// ---------------------------------------------------------- constructors ---

String::String(const char* cstr) {
  init();
  if (cstr) copy(cstr, (unsigned int)strlen(cstr));
}

String::String(const String& value) {
  init();
  *this = value;
}

String::String(const __FlashStringHelper* str) {
  init();
  const char* p = flash_str(str);
  if (p) copy(p, (unsigned int)strlen(p));
}

String::String(char c) {
  init();
  char buf[2] = {c, '\0'};
  copy(buf, 1u);
}

String::String(unsigned char value, unsigned char base) {
  init();
  char buf[1 + 8 * sizeof(unsigned char)];
  utoa(value, buf, base);
  *this = buf;
}

String::String(int value, unsigned char base) {
  init();
  char buf[2 + 8 * sizeof(int)];
  itoa(value, buf, base);
  *this = buf;
}

String::String(unsigned int value, unsigned char base) {
  init();
  char buf[1 + 8 * sizeof(unsigned int)];
  utoa(value, buf, base);
  *this = buf;
}

String::String(long value, unsigned char base) {
  init();
  char buf[2 + 8 * sizeof(long)];
  ltoa(value, buf, base);
  *this = buf;
}

String::String(unsigned long value, unsigned char base) {
  init();
  char buf[1 + 8 * sizeof(unsigned long)];
  ultoa(value, buf, base);
  *this = buf;
}

String::String(float value, unsigned char decimalPlaces) {
  init();
  char buf[40];
  snprintf(buf, sizeof(buf), "%.*f", (int)decimalPlaces, (double)value);
  *this = buf;
}

String::String(double value, unsigned char decimalPlaces) {
  init();
  char buf[40];
  snprintf(buf, sizeof(buf), "%.*f", (int)decimalPlaces, value);
  *this = buf;
}

String::~String() {
  free(buffer_);
}

void String::clear(void) {
  length_ = 0u;
  if (buffer_) buffer_[0] = '\0';
}

// ------------------------------------------------------------ assignment ---

String& String::operator=(const String& rhs) {
  if (this == &rhs) return *this;
  if (rhs.buffer_) {
    copy(rhs.buffer_, rhs.length_);
  } else {
    invalidate();
  }
  return *this;
}

String& String::operator=(const char* cstr) {
  if (cstr) {
    copy(cstr, (unsigned int)strlen(cstr));
  } else {
    invalidate();
  }
  return *this;
}

String& String::operator=(const __FlashStringHelper* str) {
  return *this = flash_str(str);
}

// --------------------------------------------------------------- concat ---

bool String::concat(const char* cstr, unsigned int length) {
  const unsigned int newlen = length_ + length;
  if (!cstr) return false;
  if (length == 0u) return true;
  if (!reserve(newlen)) return false;
  memcpy(buffer_ + length_, cstr, length);
  buffer_[newlen] = '\0';
  length_ = newlen;
  return true;
}

bool String::concat(const String& s) { return concat(s.buffer_, s.length_); }

bool String::concat(const char* cstr) {
  if (!cstr) return false;
  return concat(cstr, (unsigned int)strlen(cstr));
}

bool String::concat(char c) { return concat(&c, 1u); }

bool String::concat(unsigned char num) {
  char buf[1 + 3 * sizeof(unsigned char)];
  utoa(num, buf, 10);
  return concat(buf);
}

bool String::concat(int num) {
  char buf[2 + 3 * sizeof(int)];
  itoa(num, buf, 10);
  return concat(buf);
}

bool String::concat(unsigned int num) {
  char buf[1 + 3 * sizeof(unsigned int)];
  utoa(num, buf, 10);
  return concat(buf);
}

bool String::concat(long num) {
  char buf[2 + 3 * sizeof(long)];
  ltoa(num, buf, 10);
  return concat(buf);
}

bool String::concat(unsigned long num) {
  char buf[1 + 3 * sizeof(unsigned long)];
  ultoa(num, buf, 10);
  return concat(buf);
}

bool String::concat(float num) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%.2f", (double)num);
  return concat(buf);
}

bool String::concat(double num) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%.2f", num);
  return concat(buf);
}

bool String::concat(const __FlashStringHelper* str) {
  return concat(flash_str(str));
}

// The helper is the accumulating left-hand side, so each + appends in place.
static StringSumHelper& append(const StringSumHelper& lhs, const char* cstr) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!cstr || !a.concat(cstr)) a = "";
  return a;
}

StringSumHelper& operator+(const StringSumHelper& lhs, const String& rhs) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(rhs)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, const char* cstr) {
  return append(lhs, cstr);
}
StringSumHelper& operator+(const StringSumHelper& lhs, char c) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(c)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned char num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, int num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned int num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, long num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned long num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, float num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs, double num) {
  StringSumHelper& a = const_cast<StringSumHelper&>(lhs);
  if (!a.concat(num)) a = "";
  return a;
}
StringSumHelper& operator+(const StringSumHelper& lhs,
                           const __FlashStringHelper* rhs) {
  return append(lhs, flash_str(rhs));
}

// ------------------------------------------------------------ comparison ---

int String::compareTo(const String& s) const {
  if (!buffer_ || !s.buffer_) {
    if (s.buffer_ && s.length_ > 0u) return 0 - *(unsigned char*)s.buffer_;
    if (buffer_ && length_ > 0u) return *(unsigned char*)buffer_;
    return 0;
  }
  return strcmp(buffer_, s.buffer_);
}

bool String::equals(const String& s2) const {
  return (length_ == s2.length_) && (compareTo(s2) == 0);
}

bool String::equals(const char* cstr) const {
  if (length_ == 0u) return !cstr || *cstr == '\0';
  if (!cstr) return false;
  return strcmp(buffer_, cstr) == 0;
}

bool String::equalsIgnoreCase(const String& s2) const {
  if (this == &s2) return true;
  if (length_ != s2.length_) return false;
  if (length_ == 0u) return true;
  const char* p1 = buffer_;
  const char* p2 = s2.buffer_;
  while (*p1) {
    char a = *p1++;
    char b = *p2++;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

bool String::startsWith(const String& s2) const {
  if (length_ < s2.length_) return false;
  return startsWith(s2, 0u);
}

bool String::startsWith(const String& s2, unsigned int offset) const {
  if (offset > length_ - s2.length_ || !buffer_ || !s2.buffer_) return false;
  return strncmp(&buffer_[offset], s2.buffer_, s2.length_) == 0;
}

bool String::endsWith(const String& s2) const {
  if (length_ < s2.length_ || !buffer_ || !s2.buffer_) return false;
  return strcmp(&buffer_[length_ - s2.length_], s2.buffer_) == 0;
}

// -------------------------------------------------------------- elements ---

char String::charAt(unsigned int loc) const { return operator[](loc); }

void String::setCharAt(unsigned int loc, char c) {
  if (loc < length_) buffer_[loc] = c;
}

char String::operator[](unsigned int index) const {
  if (index >= length_ || !buffer_) return 0;
  return buffer_[index];
}

char& String::operator[](unsigned int index) {
  static char dummy_writable_char;
  if (index >= length_ || !buffer_) {
    dummy_writable_char = 0;
    return dummy_writable_char;
  }
  return buffer_[index];
}

void String::getBytes(unsigned char* buf, unsigned int bufsize,
                      unsigned int index) const {
  if (!bufsize || !buf) return;
  if (index >= length_) {
    buf[0] = 0;
    return;
  }
  unsigned int n = bufsize - 1u;
  if (n > length_ - index) n = length_ - index;
  memcpy(buf, buffer_ + index, n);
  buf[n] = 0;
}

// --------------------------------------------------------------- search ---

int String::indexOf(char c) const { return indexOf(c, 0u); }

int String::indexOf(char ch, unsigned int fromIndex) const {
  if (fromIndex >= length_) return -1;
  const char* temp = strchr(buffer_ + fromIndex, ch);
  if (!temp) return -1;
  return (int)(temp - buffer_);
}

int String::indexOf(const String& s2) const { return indexOf(s2, 0u); }

int String::indexOf(const String& s2, unsigned int fromIndex) const {
  if (fromIndex >= length_) return -1;
  const char* found = strstr(buffer_ + fromIndex, s2.buffer_);
  if (!found) return -1;
  return (int)(found - buffer_);
}

int String::lastIndexOf(char theChar) const {
  return lastIndexOf(theChar, length_ - 1u);
}

int String::lastIndexOf(char ch, unsigned int fromIndex) const {
  if (fromIndex >= length_) return -1;
  char tempchar = buffer_[fromIndex + 1u];
  buffer_[fromIndex + 1u] = '\0';
  char* temp = strrchr(buffer_, ch);
  buffer_[fromIndex + 1u] = tempchar;
  if (!temp) return -1;
  return (int)(temp - buffer_);
}

int String::lastIndexOf(const String& s2) const {
  return lastIndexOf(s2, length_ - s2.length_);
}

int String::lastIndexOf(const String& s2, unsigned int fromIndex) const {
  if (s2.length_ == 0u || s2.length_ > length_) return -1;
  if (fromIndex >= length_) fromIndex = length_ - 1u;
  int found = -1;
  for (char* p = buffer_; p <= buffer_ + fromIndex; p++) {
    p = strstr(p, s2.buffer_);
    if (!p) break;
    if ((unsigned int)(p - buffer_) <= fromIndex) found = (int)(p - buffer_);
  }
  return found;
}

String String::substring(unsigned int left, unsigned int right) const {
  if (left > right) {
    const unsigned int temp = right;
    right = left;
    left = temp;
  }
  String out;
  if (left >= length_) return out;
  if (right > length_) right = length_;
  char temp = buffer_[right];
  buffer_[right] = '\0';
  out = buffer_ + left;
  buffer_[right] = temp;
  return out;
}

// ------------------------------------------------------------ modifiers ---

void String::replace(char find, char replace) {
  if (!buffer_) return;
  for (char* p = buffer_; *p; p++) {
    if (*p == find) *p = replace;
  }
}

void String::replace(const String& find, const String& replace) {
  if (length_ == 0u || find.length_ == 0u) return;
  const int diff = (int)replace.length_ - (int)find.length_;
  char* readFrom = buffer_;
  char* foundAt;

  if (diff == 0) {
    while ((foundAt = strstr(readFrom, find.buffer_)) != nullptr) {
      memcpy(foundAt, replace.buffer_, replace.length_);
      readFrom = foundAt + replace.length_;
    }
  } else if (diff < 0) {
    char* writeTo = buffer_;
    while ((foundAt = strstr(readFrom, find.buffer_)) != nullptr) {
      const unsigned int n = (unsigned int)(foundAt - readFrom);
      memcpy(writeTo, readFrom, n);
      writeTo += n;
      memcpy(writeTo, replace.buffer_, replace.length_);
      writeTo += replace.length_;
      readFrom = foundAt + find.length_;
      length_ -= (unsigned int)(-diff);
    }
    strcpy(writeTo, readFrom);
  } else {
    unsigned int size = length_;
    while ((foundAt = strstr(readFrom, find.buffer_)) != nullptr) {
      readFrom = foundAt + find.length_;
      size += (unsigned int)diff;
    }
    if (size == length_) return;
    if (size > capacity_ && !changeBuffer(size)) return;  // no room: give up
    int index = (int)length_ - 1;
    while (index >= 0 && (index = lastIndexOf(find, (unsigned int)index)) >= 0) {
      readFrom = buffer_ + index + find.length_;
      memmove(readFrom + diff, readFrom, length_ - (unsigned int)(readFrom - buffer_));
      length_ += (unsigned int)diff;
      buffer_[length_] = '\0';
      memcpy(buffer_ + index, replace.buffer_, replace.length_);
      index--;
    }
  }
}

void String::remove(unsigned int index) {
  if (index < length_) remove(index, length_ - index);
}

void String::remove(unsigned int index, unsigned int count) {
  if (index >= length_ || count == 0u) return;
  if (count > length_ - index) count = length_ - index;
  char* writeTo = buffer_ + index;
  length_ -= count;
  memmove(writeTo, writeTo + count, length_ - index);
  buffer_[length_] = '\0';
}

void String::toLowerCase(void) {
  if (!buffer_) return;
  for (char* p = buffer_; *p; p++) {
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
  }
}

void String::toUpperCase(void) {
  if (!buffer_) return;
  for (char* p = buffer_; *p; p++) {
    if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
  }
}

void String::trim(void) {
  if (!buffer_ || length_ == 0u) return;
  char* begin = buffer_;
  while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') {
    begin++;
  }
  char* end = buffer_ + length_ - 1;
  while (end >= begin &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    end--;
  }
  length_ = (unsigned int)(end + 1 - begin);
  if (begin > buffer_) memmove(buffer_, begin, length_);
  buffer_[length_] = '\0';
}

// ------------------------------------------------------------ conversion ---

long String::toInt(void) const {
  if (!buffer_) return 0;
  return atol(buffer_);
}

float String::toFloat(void) const { return (float)toDouble(); }

double String::toDouble(void) const {
  if (!buffer_) return 0.0;
  return atof(buffer_);
}
