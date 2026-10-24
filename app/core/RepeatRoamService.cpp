// SPDX-License-Identifier: Apache-2.0
// app/core/RepeatRoamService.cpp — Automatic Repeater Discovery & Roaming ("Repeat-Roam")
#include "RepeatRoamService.h"
#include <string.h>

namespace faro::core {

float RepeatRoamService::computeScore(const RepeaterCandidate& c, uint32_t now_ms) {
  if (!c.valid) return -1e9f;
  uint32_t age_ms = (now_ms >= c.last_heard_ms) ? (now_ms - c.last_heard_ms) : 0;
  if (age_ms > kStaleTimeoutMs) return -1e9f;
  float snr_db = float(c.snr_q4) / 4.0f;
  float age_sec = float(age_ms) / 1000.0f;
  return (snr_db * 4.0f) + (float(c.rssi) * 0.25f) - (age_sec * 0.1f);
}

void RepeatRoamService::onRepeaterHeard(const uint8_t pub_key[32], const char* name,
                                        int8_t snr_q4, int16_t rssi,
                                        int32_t lat_e6, int32_t lon_e6,
                                        uint32_t now_ms) {
  if (!pub_key) return;
  int slot = -1;
  uint32_t oldest_ms = UINT32_MAX;
  int oldest_idx = 0;

  for (size_t i = 0; i < kMaxCandidates; ++i) {
    if (_candidates[i].valid && memcmp(_candidates[i].pub_key, pub_key, 32) == 0) {
      slot = int(i);
      break;
    }
    if (!_candidates[i].valid && slot < 0) {
      slot = int(i);
    }
    if (_candidates[i].last_heard_ms < oldest_ms) {
      oldest_ms = _candidates[i].last_heard_ms;
      oldest_idx = int(i);
    }
  }
  if (slot < 0) slot = oldest_idx;

  RepeaterCandidate& dst = _candidates[slot];
  memcpy(dst.pub_key, pub_key, 32);
  if (name && name[0]) {
    strncpy(dst.name, name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = '\0';
  } else if (!dst.name[0]) {
    strncpy(dst.name, "Repeater", sizeof(dst.name) - 1);
  }
  dst.snr_q4 = snr_q4;
  dst.rssi = rssi;
  dst.last_heard_ms = now_ms;
  if (lat_e6 != 0 || lon_e6 != 0) {
    dst.lat_e6 = lat_e6;
    dst.lon_e6 = lon_e6;
  }
  dst.is_preferred = (_has_preferred && memcmp(pub_key, _preferred_pubkey, 32) == 0);
  dst.valid = true;
}

bool RepeatRoamService::evaluate(uint32_t now_ms, uint8_t out_new_preferred[32]) {
  if (!_enabled) return false;

  int best_idx = -1;
  float best_score = -1e8f;
  int curr_idx = -1;
  float curr_score = -1e8f;

  for (size_t i = 0; i < kMaxCandidates; ++i) {
    if (!_candidates[i].valid) continue;
    float s = computeScore(_candidates[i], now_ms);
    if (s <= -1e8f) continue;  // stale
    if (_has_preferred && memcmp(_candidates[i].pub_key, _preferred_pubkey, 32) == 0) {
      curr_idx = int(i);
      curr_score = s;
    }
    if (s > best_score) {
      best_score = s;
      best_idx = int(i);
    }
  }

  if (best_idx < 0) return false;

  bool should_switch = false;
  if (!_has_preferred || curr_idx < 0) {
    // No current valid preferred repeater -> pick best immediately
    should_switch = true;
  } else if (best_idx != curr_idx) {
    bool cooldown_elapsed = (_last_switch_ms == 0) ||
                            (now_ms >= _last_switch_ms && (now_ms - _last_switch_ms) >= kSwitchCooldownMs);
    if (cooldown_elapsed && (best_score - curr_score) >= kHysteresisScore) {
      should_switch = true;
    }
  }

  if (!should_switch) return false;

  _has_preferred = true;
  memcpy(_preferred_pubkey, _candidates[best_idx].pub_key, 32);
  _last_switch_ms = now_ms;
  for (size_t i = 0; i < kMaxCandidates; ++i) {
    _candidates[i].is_preferred = (int(i) == best_idx);
  }
  if (out_new_preferred) {
    memcpy(out_new_preferred, _preferred_pubkey, 32);
  }
  return true;
}

const char* RepeatRoamService::preferredName() const {
  if (!_has_preferred) return "None";
  for (size_t i = 0; i < kMaxCandidates; ++i) {
    if (_candidates[i].valid && memcmp(_candidates[i].pub_key, _preferred_pubkey, 32) == 0) {
      return _candidates[i].name;
    }
  }
  return "Repeater";
}

size_t RepeatRoamService::candidateCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxCandidates; ++i) {
    if (_candidates[i].valid) ++n;
  }
  return n;
}

}  // namespace faro::core
