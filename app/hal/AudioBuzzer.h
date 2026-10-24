// SPDX-License-Identifier: Apache-2.0
// AudioBuzzer.h — Non-blocking audio alert driver for T-Deck Plus
#pragma once

#include <stdint.h>

namespace faro::hal {

enum class TonePattern : uint8_t {
  None = 0,
  MessageChime,
  ChannelChime,
  AdvertDiscovered,
  AlertTone,
  SosAlarm
};

class AudioBuzzer {
public:
  void begin();
  void setQuiet(bool quiet) { _quiet = quiet; }
  bool isQuiet() const { return _quiet; }

  void play(TonePattern pattern);
  void stop();
  void loop(uint32_t now_ms);

private:
  bool _quiet = false;
  TonePattern _active = TonePattern::None;
  uint32_t _step_start_ms = 0;
  uint8_t _step = 0;
};

}  // namespace faro::hal
