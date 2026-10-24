// SPDX-License-Identifier: Apache-2.0
// PowerBoard.cpp — T-Deck Plus peripheral power (GPIO 10) & battery ADC (GPIO 4)
#include "PowerBoard.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace faro::hal {

void PowerBoard::begin() {
#ifdef ARDUINO
#if defined(LILYGO_TDECK)
  pinMode(kPinPowerOn, OUTPUT);
  digitalWrite(kPinPowerOn, HIGH);
#elif defined(THINKNODE_M9)
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);
#endif
#ifdef PIN_VBAT_READ
  pinMode(PIN_VBAT_READ, INPUT);
#else
  pinMode(kPinBattAdc, INPUT);
#endif
  analogReadResolution(12);
#endif
}

uint16_t PowerBoard::readBattMilliVolts() {
#ifdef ARDUINO
  uint32_t acc = 0;
#ifdef PIN_VBAT_READ
  const int adc_pin = PIN_VBAT_READ;
#else
  const int adc_pin = kPinBattAdc;
#endif
  for (int i = 0; i < 8; ++i) {
    acc += analogRead(adc_pin);
  }
  uint32_t avg = acc / 8U;
  // TDeckBoard formula: 2 * 3.3V * 1000 / 4096
  uint16_t mv = uint16_t((avg * 6600U) / 4096U);
  if (_last_mv > 0) {
    if (mv > _last_mv + 25 || mv >= 4185) {
      _charging = true;
    } else if (mv + 15 < _last_mv) {
      _charging = false;
    }
  }
  _last_mv = mv;
  return mv;
#else
  return 4050;
#endif
}

uint8_t PowerBoard::readBattPercent(uint16_t mv) const {
  if (mv <= 3300) return 0;
  if (mv >= 4200) return 100;
  // Piecewise LiPo discharge curve approximation
  if (mv >= 3700) {
    // 3700..4200 mV -> 20..100%
    return uint8_t(20 + ((uint32_t(mv - 3700) * 80U) / 500U));
  }
  // 3300..3700 mV -> 0..20%
  return uint8_t((uint32_t(mv - 3300) * 20U) / 400U);
}

}  // namespace faro::hal
