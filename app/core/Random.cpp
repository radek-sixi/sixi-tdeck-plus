// SPDX-License-Identifier: Apache-2.0
// app/core/Random.cpp — Cryptographic random bytes (hardware RNG on ESP32, OS RNG on the host)
#include "Random.h"

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

namespace faro::core {

void fillRandom(uint8_t* buf, size_t len) {
  if (!buf || len == 0) return;
#if defined(ESP_PLATFORM)
  // True RNG while the radio (Wi-Fi/BT) runs; Faro always has BLE up (esp_random docs).
  esp_fill_random(buf, len);
#else
  FILE* f = fopen("/dev/urandom", "rb");
  if (!f || fread(buf, 1, len, f) != len) abort();  // fail closed: no key from a weak source
  fclose(f);
#endif
}

bool isWeakBlePin(uint32_t pin) {
  if (pin < 100000 || pin > 999999) return true;
  uint8_t d[6];
  for (int i = 5; i >= 0; --i) { d[i] = uint8_t(pin % 10); pin /= 10; }
  bool same = true, up = true, down = true;
  for (int i = 1; i < 6; ++i) {
    same &= d[i] == d[0];
    up &= d[i] == (d[i - 1] + 1) % 10;
    down &= d[i] == (d[i - 1] + 9) % 10;
  }
  return same || up || down;  // 111111, 123456, 654321 ...
}

uint32_t randomBlePin() {
  for (;;) {
    uint8_t b[4];
    fillRandom(b, sizeof(b));
    uint32_t v = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    if (v >= 4294000000U) continue;  // reject the tail so the modulo stays uniform
    uint32_t pin = 100000U + v % 900000U;
    if (!isWeakBlePin(pin)) return pin;
  }
}

}  // namespace faro::core
