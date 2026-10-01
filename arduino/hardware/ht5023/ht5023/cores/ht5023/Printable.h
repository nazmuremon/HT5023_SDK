// Printable.h -- lets a class hand itself to Serial.print().
//
// Libraries use this for value types (IPAddress, sensor readings, timestamps):
// derive, implement printTo(), and print(obj) works everywhere Print does.
#pragma once

#include <stddef.h>

class Print;

class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};
