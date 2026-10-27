// SPDX-License-Identifier: Apache-2.0
// KeypadM9.cpp — Elecrow ThinkNode M9 keypad (STC8H helper MCU, I2C 0x6C on Wire1)
#if defined(ARDUINO) && defined(THINKNODE_M9)
#include <Arduino.h>
#include <Wire.h>
#include "KeypadM9.h"

namespace faro::hal {

namespace {
volatile bool g_key_irq = false;
void IRAM_ATTR onKeyIrq() { g_key_irq = true; }  // KB_INT idles low and pulses high on a key press
}  // namespace

bool KeypadM9::begin() {
  pinMode(kPinInt, INPUT);
  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);
  Wire1.begin(kPinSda, kPinScl, 100000);
  Wire1.beginTransmission(kI2cAddr);
  _available = (Wire1.endTransmission() == 0);
  if (_available) attachInterrupt(digitalPinToInterrupt(kPinInt), onKeyIrq, RISING);
  Serial.printf("[FARO] M9 keypad (0x%02X): %s\n", kI2cAddr, _available ? "found" : "not found");
  return _available;
}

uint8_t KeypadM9::readKey(uint32_t now_ms) {
  if (!_available) return 0;
  const bool irq = g_key_irq;
  // Without an interrupt, poll slowly as a safety net (the same code twice in a row is ignored then)
  if (!irq && now_ms - _last_poll_ms < 250) return 0;
  _last_poll_ms = now_ms;
  g_key_irq = false;
  Wire1.beginTransmission(kI2cAddr);
  Wire1.write(kRegKey);
  if (Wire1.endTransmission(false) != 0) return 0;
  if (Wire1.requestFrom(kI2cAddr, uint8_t(1)) != 1) return 0;
  const uint8_t k = uint8_t(Wire1.read());
  if (k == 0 || k == 0xFF) {
    _last_key = 0;
    return 0;
  }
  if (!irq && k == _last_key) return 0;
  _last_key = k;
  const bool known = (k >= 0x20 && k < 0x7F) || k == 0x0D || k == 0x08 || k == 0x1B ||
                     (k >= 0x81 && k <= 0x86);
  if (!known) Serial.printf("[FARO] M9 keypad: unmapped key code 0x%02X\n", unsigned(k));
  return k;
}

}  // namespace faro::hal
#endif
