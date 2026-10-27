// SPDX-License-Identifier: Apache-2.0
// AudioBuzzer.cpp — Non-blocking alert tones on boards with a piezo (PIN_BUZZER, e.g. ThinkNode M9).
// Boards without one (T-Deck: I2S speaker, not driven) keep the state machine silent.
#include "AudioBuzzer.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace faro::hal {

namespace {
struct ToneStep {
  uint16_t freq_hz;  // 0 = pause
  uint16_t ms;
};

const ToneStep kMessage[] = {{1800, 70}, {0, 60}, {2400, 90}};
const ToneStep kChannel[] = {{2000, 60}};
const ToneStep kAdvert[] = {{3000, 30}};
const ToneStep kAlert[] = {{2500, 150}, {0, 100}, {2500, 150}, {0, 100}, {2500, 150}};
const ToneStep kSos[] = {{3000, 200}, {2000, 200}, {3000, 200}, {2000, 200}, {3000, 200}, {2000, 200}};

const ToneStep* stepsFor(TonePattern p, uint8_t& count) {
  switch (p) {
    case TonePattern::MessageChime:     count = 3; return kMessage;
    case TonePattern::ChannelChime:     count = 1; return kChannel;
    case TonePattern::AdvertDiscovered: count = 1; return kAdvert;
    case TonePattern::AlertTone:        count = 5; return kAlert;
    case TonePattern::SosAlarm:         count = 6; return kSos;
    case TonePattern::None:
    default:                            count = 0; return nullptr;
  }
}

void startTone(uint16_t freq_hz, uint16_t ms) {
#if defined(ARDUINO) && defined(PIN_BUZZER)
  if (freq_hz > 0) {
    tone(PIN_BUZZER, freq_hz, ms);
  } else {
    noTone(PIN_BUZZER);
  }
#else
  (void)freq_hz;
  (void)ms;
#endif
}
}  // namespace

void AudioBuzzer::begin() {
  _active = TonePattern::None;
  _step = 0;
#if defined(ARDUINO) && defined(PIN_BUZZER)
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
#endif
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
  uint8_t count = 0;
  const ToneStep* steps = stepsFor(pattern, count);
  if (steps && count) startTone(steps[0].freq_hz, steps[0].ms);
}

void AudioBuzzer::stop() {
  _active = TonePattern::None;
  _step = 0;
  startTone(0, 0);
}

void AudioBuzzer::loop(uint32_t now_ms) {
  if (_active == TonePattern::None) return;
  uint8_t count = 0;
  const ToneStep* steps = stepsFor(_active, count);
  if (!steps || _step >= count) {
    stop();
    return;
  }
  if (_step_start_ms == 0) _step_start_ms = now_ms;
  if (now_ms - _step_start_ms < steps[_step].ms) return;
  ++_step;
  _step_start_ms = now_ms;
  if (_step >= count) {
    stop();
    return;
  }
  startTone(steps[_step].freq_hz, steps[_step].ms);
}

}  // namespace faro::hal
