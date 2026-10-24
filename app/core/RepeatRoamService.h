// SPDX-License-Identifier: Apache-2.0
// app/core/RepeatRoamService.h — Automatic Repeater Discovery & Roaming ("Repeat-Roam")
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Types.h"

namespace faro::core {

class RepeatRoamService {
public:
  static constexpr size_t   kMaxCandidates    = 32;
  static constexpr uint32_t kStaleTimeoutMs   = 300000;  // 5 minutes
  static constexpr uint32_t kSwitchCooldownMs = 30000;   // 30 seconds
  static constexpr float    kHysteresisScore  = 12.0f;   // ~3.0 dB SNR improvement

  void setEnabled(bool en) { _enabled = en; }
  bool isEnabled() const { return _enabled; }

  void onRepeaterHeard(const uint8_t pub_key[32], const char* name,
                       int8_t snr_q4, int16_t rssi,
                       int32_t lat_e6, int32_t lon_e6,
                       uint32_t now_ms);

  // Evaluates candidates at now_ms. Returns true if the preferred repeater changed.
  bool evaluate(uint32_t now_ms, uint8_t out_new_preferred[32]);

  bool hasPreferred() const { return _has_preferred; }
  const uint8_t* preferredPubKey() const { return _preferred_pubkey; }
  const char* preferredName() const;

  size_t candidateCount() const;
  const RepeaterCandidate* candidates() const { return _candidates; }

  static float computeScore(const RepeaterCandidate& c, uint32_t now_ms);

private:
  bool _enabled = false;
  bool _has_preferred = false;
  uint8_t _preferred_pubkey[32] = {0};
  uint32_t _last_switch_ms = 0;
  RepeaterCandidate _candidates[kMaxCandidates] = {};
};

}  // namespace faro::core
