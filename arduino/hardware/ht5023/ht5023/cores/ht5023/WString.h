// WString.h -- the Arduino String class.
//
// Third-party libraries lean on this heavily (parsing responses, building
// commands), so the whole documented API is here rather than a subset: a
// missing method shows up as a compile error deep inside somebody else's
// library, which is a miserable thing to debug.
//
// The buffer is heap-allocated and grows in place. An allocation failure
// leaves the string empty and invalid, which is what `if (str)` tests for.
#pragma once

#include <stddef.h>
#include <stdint.h>

class __FlashStringHelper;
class String;

class StringSumHelper;

class String {
  // Lets `if (str)` work without allowing `int x = str;`
  typedef void (String::*StringIfHelperType)() const;
  void StringIfHelper() const {}

 public:
  String(const char* cstr = "");
  String(const String& value);
  String(const __FlashStringHelper* str);
  explicit String(char c);
  explicit String(unsigned char value, unsigned char base = 10u);
  explicit String(int value, unsigned char base = 10u);
  explicit String(unsigned int value, unsigned char base = 10u);
  explicit String(long value, unsigned char base = 10u);
  explicit String(unsigned long value, unsigned char base = 10u);
  explicit String(float value, unsigned char decimalPlaces = 2u);
  explicit String(double value, unsigned char decimalPlaces = 2u);
  ~String();

  // Reserves at least `size` characters. False means the allocation failed.
  bool reserve(unsigned int size);
  unsigned int length(void) const { return length_; }
  void clear(void);
  bool isEmpty(void) const { return length_ == 0u; }

  String& operator=(const String& rhs);
  String& operator=(const char* cstr);
  String& operator=(const __FlashStringHelper* str);

  bool concat(const String& str);
  bool concat(const char* cstr);
  bool concat(const char* cstr, unsigned int length);
  bool concat(char c);
  bool concat(unsigned char value);
  bool concat(int value);
  bool concat(unsigned int value);
  bool concat(long value);
  bool concat(unsigned long value);
  bool concat(float value);
  bool concat(double value);
  bool concat(const __FlashStringHelper* str);

  String& operator+=(const String& rhs) { concat(rhs); return *this; }
  String& operator+=(const char* cstr) { concat(cstr); return *this; }
  String& operator+=(char c) { concat(c); return *this; }
  String& operator+=(unsigned char num) { concat(num); return *this; }
  String& operator+=(int num) { concat(num); return *this; }
  String& operator+=(unsigned int num) { concat(num); return *this; }
  String& operator+=(long num) { concat(num); return *this; }
  String& operator+=(unsigned long num) { concat(num); return *this; }
  String& operator+=(float num) { concat(num); return *this; }
  String& operator+=(double num) { concat(num); return *this; }
  String& operator+=(const __FlashStringHelper* str) { concat(str); return *this; }

  operator StringIfHelperType() const {
    return buffer_ ? &String::StringIfHelper : 0;
  }

  int compareTo(const String& s) const;
  bool equals(const String& s) const;
  bool equals(const char* cstr) const;
  bool equalsIgnoreCase(const String& s) const;

  bool operator==(const String& rhs) const { return equals(rhs); }
  bool operator==(const char* cstr) const { return equals(cstr); }
  bool operator!=(const String& rhs) const { return !equals(rhs); }
  bool operator!=(const char* cstr) const { return !equals(cstr); }
  bool operator<(const String& rhs) const { return compareTo(rhs) < 0; }
  bool operator>(const String& rhs) const { return compareTo(rhs) > 0; }
  bool operator<=(const String& rhs) const { return compareTo(rhs) <= 0; }
  bool operator>=(const String& rhs) const { return compareTo(rhs) >= 0; }

  bool startsWith(const String& prefix) const;
  bool startsWith(const String& prefix, unsigned int offset) const;
  bool endsWith(const String& suffix) const;

  char charAt(unsigned int index) const;
  void setCharAt(unsigned int index, char c);
  char operator[](unsigned int index) const;
  char& operator[](unsigned int index);
  void getBytes(unsigned char* buf, unsigned int bufsize,
                unsigned int index = 0u) const;
  void toCharArray(char* buf, unsigned int bufsize,
                   unsigned int index = 0u) const {
    getBytes((unsigned char*)buf, bufsize, index);
  }
  const char* c_str() const { return buffer_ ? buffer_ : ""; }
  char* begin() { return buffer_; }
  char* end() { return buffer_ + length_; }
  const char* begin() const { return c_str(); }
  const char* end() const { return c_str() + length_; }

  int indexOf(char ch) const;
  int indexOf(char ch, unsigned int fromIndex) const;
  int indexOf(const String& str) const;
  int indexOf(const String& str, unsigned int fromIndex) const;
  int lastIndexOf(char ch) const;
  int lastIndexOf(char ch, unsigned int fromIndex) const;
  int lastIndexOf(const String& str) const;
  int lastIndexOf(const String& str, unsigned int fromIndex) const;
  String substring(unsigned int beginIndex) const {
    return substring(beginIndex, length_);
  }
  String substring(unsigned int beginIndex, unsigned int endIndex) const;

  void replace(char find, char replace);
  void replace(const String& find, const String& replace);
  void remove(unsigned int index);
  void remove(unsigned int index, unsigned int count);
  void toLowerCase(void);
  void toUpperCase(void);
  void trim(void);

  long toInt(void) const;
  float toFloat(void) const;
  double toDouble(void) const;

 protected:
  char* buffer_;
  unsigned int capacity_;   // excluding the terminating NUL
  unsigned int length_;

  void init(void);
  void invalidate(void);
  bool changeBuffer(unsigned int maxStrLen);

  String& copy(const char* cstr, unsigned int length);
  void move(String& rhs);
};

// The += chain: `a + b + c` builds through this so each step appends to one
// buffer instead of allocating a fresh String per operator.
class StringSumHelper : public String {
 public:
  StringSumHelper(const String& s) : String(s) {}
  StringSumHelper(const char* p) : String(p) {}
  StringSumHelper(char c) : String(c) {}
  StringSumHelper(unsigned char num) : String(num) {}
  StringSumHelper(int num) : String(num) {}
  StringSumHelper(unsigned int num) : String(num) {}
  StringSumHelper(long num) : String(num) {}
  StringSumHelper(unsigned long num) : String(num) {}
  StringSumHelper(float num) : String(num) {}
  StringSumHelper(double num) : String(num) {}
};

StringSumHelper& operator+(const StringSumHelper& lhs, const String& rhs);
StringSumHelper& operator+(const StringSumHelper& lhs, const char* cstr);
StringSumHelper& operator+(const StringSumHelper& lhs, char c);
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned char num);
StringSumHelper& operator+(const StringSumHelper& lhs, int num);
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned int num);
StringSumHelper& operator+(const StringSumHelper& lhs, long num);
StringSumHelper& operator+(const StringSumHelper& lhs, unsigned long num);
StringSumHelper& operator+(const StringSumHelper& lhs, float num);
StringSumHelper& operator+(const StringSumHelper& lhs, double num);
StringSumHelper& operator+(const StringSumHelper& lhs,
                           const __FlashStringHelper* rhs);

inline bool operator==(const char* lhs, const String& rhs) {
  return rhs == lhs;
}
inline bool operator!=(const char* lhs, const String& rhs) {
  return rhs != lhs;
}
