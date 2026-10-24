// SPDX-License-Identifier: Apache-2.0
// TouchGT911.h — GT911 capacitive touch controller on T-Deck Plus I2C bus (SDA=18, SCL=8, INT=16)
#pragma once

#include <stdint.h>

namespace faro::hal {

class TouchGT911 {
public:
  static constexpr uint8_t kAddrPrimary = 0x5D;
  static constexpr uint8_t kAddrAlt     = 0x14;
  static constexpr int     kPinInt      = 16;

  bool begin();
  bool isAvailable() const { return _available; }
  uint8_t address() const { return _addr; }

  // Polls GT911 status (0x814E) and first touch point (0x8150..0x8153).
  // Outputs landscape coordinates: x in [0, 319], y in [0, 239].
  bool read(int16_t& x, int16_t& y);

private:
  bool probeAddress(uint8_t addr);
  bool readRegs(uint16_t reg, uint8_t* buf, uint8_t len);
  bool writeReg(uint16_t reg, uint8_t val);

  uint8_t _addr = 0;
  bool _available = false;
  int16_t _last_x = 160;
  int16_t _last_y = 120;
};

}  // namespace faro::hal
