// SPDX-License-Identifier: Apache-2.0
// KeyboardBBQ10.h — LilyGO T-Deck ESP32-C3 keyboard controller on I2C (0x55)
#pragma once

#include <stdint.h>

namespace faro::hal {

class KeyboardBBQ10 {
public:
  static constexpr uint8_t kI2cAddr = 0x55;

  bool begin();
  bool isAvailable() const { return _available; }

  // Polls 1 byte from the keyboard controller. Returns 0 if no key is pending.
  uint8_t readKey();

  // Sets keyboard backlight brightness (0..255). Supported on LilyGO keyboard fw v2+.
  void setBacklight(uint8_t brightness);

private:
  bool _available = false;
};

}  // namespace faro::hal
