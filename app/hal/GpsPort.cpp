// SPDX-License-Identifier: Apache-2.0
// GpsPort.cpp — UART GPS baud rate selector & NMEA health monitor (RX=43, TX=44)
#include "GpsPort.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace faro::hal {

void GpsPort::begin(uint32_t baud) {
#ifdef GPS_BAUD_RATE
  _baud = GPS_BAUD_RATE;
  (void)baud;
#else
  _baud = (baud == 9600) ? 9600 : 38400;
#endif
#ifdef ARDUINO
  Serial1.updateBaudRate(_baud);
#endif
}

void GpsPort::setBaud(uint32_t baud) {
  _baud = (baud == 9600) ? 9600 : 38400;
#ifdef ARDUINO
  Serial1.updateBaudRate(_baud);
#endif
}

}  // namespace faro::hal
