// SPDX-License-Identifier: Apache-2.0
// app/core/Random.h — Cryptographic random bytes (hardware RNG on ESP32, OS RNG on the host)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace faro::core {

// Fills buf with len random bytes suitable for keys. Never a deterministic or time-seeded source.
void fillRandom(uint8_t* buf, size_t len);

// Uniform random 6-digit PIN (100000..999999) that is not weak.
uint32_t randomBlePin();

// Default, repeated-digit and ascending/descending PINs are refused.
bool isWeakBlePin(uint32_t pin);

}  // namespace faro::core
