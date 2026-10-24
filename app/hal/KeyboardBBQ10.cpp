// SPDX-License-Identifier: Apache-2.0
// KeyboardBBQ10.cpp — LilyGO T-Deck ESP32-C3 keyboard controller on I2C (0x55)
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include "KeyboardBBQ10.h"

namespace faro::hal {

bool KeyboardBBQ10::begin() {
  Wire.beginTransmission(kI2cAddr);
  _available = (Wire.endTransmission() == 0);
  return _available;
}

uint8_t KeyboardBBQ10::readKey() {
  if (!_available) return 0;
  if (Wire.requestFrom(kI2cAddr, uint8_t(1)) != 1) return 0;
  uint8_t ch = uint8_t(Wire.read());
  if (ch == 0 || ch == 0xFF) return 0;
  static uint8_t s_last_ch = 0;
  static uint32_t s_last_ms = 0;
  uint32_t now_ms = millis();
  if (ch == s_last_ch && (now_ms - s_last_ms) < 35) {
    return 0;
  }
  s_last_ch = ch;
  s_last_ms = now_ms;
  return ch;
}

void KeyboardBBQ10::setBacklight(uint8_t brightness) {
  if (!_available) return;
  // LilyGO T-Deck C3 keyboard firmware command 0x01 sets backlight brightness (0..255)
  Wire.beginTransmission(kI2cAddr);
  Wire.write(0x01);
  Wire.write(brightness);
  Wire.endTransmission();
}

}  // namespace faro::hal
#endif
