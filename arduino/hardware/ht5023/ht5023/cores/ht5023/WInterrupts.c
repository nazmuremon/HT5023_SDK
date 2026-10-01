#include "Arduino.h"

void attachInterrupt(uint8_t interrupt_num, void (*user_func)(void), int mode) {
  (void)interrupt_num;
  (void)user_func;
  (void)mode;
}

void detachInterrupt(uint8_t interrupt_num) {
  (void)interrupt_num;
}
