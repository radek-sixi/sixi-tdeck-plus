// SPDX-License-Identifier: Apache-2.0
// PowerBoard.h — T-Deck Plus peripheral power (GPIO 10) & battery ADC (GPIO 4)
#pragma once

#include <stdint.h>

namespace faro::hal {

class PowerBoard {
public:
  static constexpr int kPinPowerOn = 10;
  static constexpr int kPinBattAdc = 4;

  void begin();
  uint16_t readBattMilliVolts();
  uint8_t readBattPercent(uint16_t mv) const;
  bool isChargingEstimated() const { return _charging; }

private:
  uint16_t _last_mv = 0;
  bool _charging = false;
};

}  // namespace faro::hal
