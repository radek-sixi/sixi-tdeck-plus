// SPDX-License-Identifier: Apache-2.0
// Trackball.h — LilyGO T-Deck Hall-effect trackball driver (UP=3, DOWN=15, LEFT=1, RIGHT=2, CLICK=0)
#pragma once

#include <stdint.h>

namespace faro::hal {

class Trackball {
public:
  static constexpr int kPinUp    = 3;
  static constexpr int kPinDown  = 15;
  static constexpr int kPinLeft  = 1;
  static constexpr int kPinRight = 2;
  static constexpr int kPinClick = 0;

  void begin();

  // Drains accumulated directional pulses and click state since last poll.
  void poll(int16_t& dx, int16_t& dy, bool& clicked, bool& long_pressed);

  bool isPressedRaw() const;

private:
  uint32_t _press_start_ms = 0;
  bool _was_pressed = false;
  bool _long_fired = false;
};

}  // namespace faro::hal
