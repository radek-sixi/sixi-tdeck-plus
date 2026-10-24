// SPDX-License-Identifier: Apache-2.0
// AudioBuzzer.cpp — Non-blocking audio alert driver for T-Deck Plus
#include "AudioBuzzer.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace faro::hal {

void AudioBuzzer::begin() {
  _active = TonePattern::None;
  _step = 0;
}

void AudioBuzzer::play(TonePattern pattern) {
  if (_quiet && pattern != TonePattern::SosAlarm) {
    return;
  }
  _active = pattern;
  _step = 0;
#ifdef ARDUINO
  _step_start_ms = millis();
#else
  _step_start_ms = 0;
#endif
}

void AudioBuzzer::stop() {
  _active = TonePattern::None;
  _step = 0;
}

void AudioBuzzer::loop(uint32_t now_ms) {
  if (_active == TonePattern::None) return;
  if (_step_start_ms == 0) _step_start_ms = now_ms;
  if (now_ms - _step_start_ms > 600) {
    _active = TonePattern::None;
    _step = 0;
  }
}

}  // namespace faro::hal
