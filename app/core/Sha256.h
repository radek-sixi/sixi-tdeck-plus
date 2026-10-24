// SPDX-License-Identifier: Apache-2.0
// app/core/Sha256.h — Minimal SHA-256 (FIPS 180-4) for host builds; devices use the MeshCore one
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace faro::core {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);

}  // namespace faro::core
