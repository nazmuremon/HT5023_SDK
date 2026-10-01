#include "Arduino.h"

void setup(void);
void loop(void);

extern void initVariant(void) __attribute__((weak));
void initVariant(void) {
}

void yield(void) __attribute__((weak));
void yield(void) {
}

extern void serialEventRun(void) __attribute__((weak));

extern "C" int main(void) {
  init();
  initVariant();
  setup();

  for (;;) {
    loop();
    if (serialEventRun) serialEventRun();
    yield();
  }
}
