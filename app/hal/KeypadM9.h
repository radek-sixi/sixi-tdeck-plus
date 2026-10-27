// SPDX-License-Identifier: Apache-2.0
// KeypadM9.h — Elecrow ThinkNode M9 keypad: STC8H helper MCU at I2C 0x6C on its own bus (SDA 20, SCL 21)
#pragma once

#include <stdint.h>

namespace faro::hal {

// Register 0x05 holds the pressed key: printable ASCII as is, 0x0D enter, 0x81/0x85 page keys,
// 0x82 home, 0x83 menu, 0x86 back. Other codes (arrows) are not documented: they are logged on the
// serial console so they can be mapped (needs hardware).
class KeypadM9 {
public:
  static constexpr uint8_t kI2cAddr = 0x6C;
  static constexpr int kPinSda = 20;
  static constexpr int kPinScl = 21;
  static constexpr int kPinInt = 12;
  static constexpr int kPinBacklight = 46;
  static constexpr uint8_t kRegKey = 0x05;

  bool begin();
  bool isAvailable() const { return _available; }
  // Returns a new key code, 0 when none (read on the KB_INT pulse, polled every 250 ms otherwise)
  uint8_t readKey(uint32_t now_ms);

private:
  bool _available = false;
  uint32_t _last_poll_ms = 0;
  uint8_t _last_key = 0;
};

}  // namespace faro::hal
