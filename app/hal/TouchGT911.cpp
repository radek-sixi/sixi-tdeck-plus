// SPDX-License-Identifier: Apache-2.0
// TouchGT911.cpp — GT911 capacitive touch controller on T-Deck Plus I2C bus (SDA=18, SCL=8, INT=16)
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include "TouchGT911.h"

namespace faro::hal {

bool TouchGT911::probeAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x81);
  Wire.write(0x40);  // Product ID register
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, uint8_t(4)) != 4) return false;
  for (int i = 0; i < 4; ++i) (void)Wire.read();
  return true;
}

bool TouchGT911::begin() {
#if defined(LILYGO_TDECK)
  pinMode(kPinInt, INPUT_PULLUP);
#endif
  if (probeAddress(kAddrPrimary)) {
    _addr = kAddrPrimary;
    _available = true;
    writeReg(0x814E, 0);
    return true;
  }
  if (probeAddress(kAddrAlt)) {
    _addr = kAddrAlt;
    _available = true;
    writeReg(0x814E, 0);
    return true;
  }
  _available = false;
  return false;
}

bool TouchGT911::readRegs(uint16_t reg, uint8_t* buf, uint8_t len) {
  if (!_available) return false;
  Wire.beginTransmission(_addr);
  Wire.write(uint8_t(reg >> 8));
  Wire.write(uint8_t(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(_addr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) {
    buf[i] = uint8_t(Wire.read());
  }
  return true;
}

bool TouchGT911::writeReg(uint16_t reg, uint8_t val) {
  if (!_available) return false;
  Wire.beginTransmission(_addr);
  Wire.write(uint8_t(reg >> 8));
  Wire.write(uint8_t(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool TouchGT911::read(int16_t& x, int16_t& y) {
  if (!_available) {
    x = _last_x;
    y = _last_y;
    return false;
  }
  uint8_t status = 0;
  if (!readRegs(0x814E, &status, 1)) {
    x = _last_x;
    y = _last_y;
    return false;
  }
  bool buffer_ready = (status & 0x80) != 0;
  uint8_t points = status & 0x0F;
  if (!buffer_ready) {
    x = _last_x;
    y = _last_y;
    return false;
  }
  if (points == 0 || points > 5) {
    writeReg(0x814E, 0);
    x = _last_x;
    y = _last_y;
    return false;
  }
  uint8_t pt[4] = {0};
  bool ok = readRegs(0x8150, pt, 4);
  writeReg(0x814E, 0);
  if (!ok) {
    x = _last_x;
    y = _last_y;
    return false;
  }
  uint16_t raw_x = uint16_t(pt[0]) | (uint16_t(pt[1]) << 8);
  uint16_t raw_y = uint16_t(pt[2]) | (uint16_t(pt[3]) << 8);

  // T-Deck rotation=1 landscape transform: raw portrait (240x320) -> landscape (320x240)
  int16_t lx = int16_t(raw_y);
  int16_t ly = int16_t(239) - int16_t(raw_x);
  if (lx < 0) lx = 0;
  if (lx > 319) lx = 319;
  if (ly < 0) ly = 0;
  if (ly > 239) ly = 239;

  _last_x = lx;
  _last_y = ly;
  x = lx;
  y = ly;
  return true;
}

}  // namespace faro::hal
#endif
