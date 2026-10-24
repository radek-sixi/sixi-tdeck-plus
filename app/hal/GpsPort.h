// SPDX-License-Identifier: Apache-2.0
// GpsPort.h — UART GPS baud rate selector & NMEA health monitor (RX=43, TX=44)
#pragma once

#include <stdint.h>

namespace faro::hal {

class GpsPort {
public:
  static constexpr int kPinRx = 43;
  static constexpr int kPinTx = 44;

  void begin(uint32_t baud = 38400);
  void setBaud(uint32_t baud);
  uint32_t baud() const { return _baud; }

  void recordSentence() { _sentence_count++; }
  uint32_t sentenceCount() const { return _sentence_count; }

private:
  uint32_t _baud = 38400;
  uint32_t _sentence_count = 0;
};

}  // namespace faro::hal
