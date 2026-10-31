// SPDX-License-Identifier: Apache-2.0
// AudioBuzzer.cpp — Non-blocking alert tones on boards with a piezo (PIN_BUZZER, e.g. ThinkNode M9)
// or an I2S speaker amplifier (LilyGo T-Deck Plus: BCLK=7, WS=5, DOUT=6).
#include "AudioBuzzer.h"

#ifdef ARDUINO
#include <Arduino.h>
#if defined(LILYGO_TDECK)
#include <driver/i2s.h>
#endif
#endif

namespace faro::hal {

namespace {
#if defined(ARDUINO) && defined(LILYGO_TDECK)
bool s_i2s_ready = false;
#endif

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
#elif defined(ARDUINO) && defined(LILYGO_TDECK)
  if (!s_i2s_ready) return;
  i2s_zero_dma_buffer(I2S_NUM_0);
  if (freq_hz == 0 || ms == 0) return;
  uint32_t total_frames = (8000U * uint32_t(ms)) / 1000U;
  if (total_frames > 1600U) total_frames = 1600U;
  int16_t chunk[128];  // 64 stereo frames
  uint32_t period_q8 = (8000U << 8) / uint32_t(freq_hz > 100 ? freq_hz : 100);
  if (period_q8 == 0) period_q8 = 256;
  uint32_t done = 0;
  while (done < total_frames) {
    uint32_t n = total_frames - done;
    if (n > 64U) n = 64U;
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t idx = done + i;
      uint32_t phase = ((idx << 8) % period_q8) * 256U / period_q8;
      int32_t tri = (phase < 128U) ? (int32_t(phase) * 4 - 256) : (768 - int32_t(phase) * 4);
      int32_t amp = 5500;
      if (idx < 32U) amp = (amp * int32_t(idx)) / 32;
      else if (total_frames - idx < 32U) amp = (amp * int32_t(total_frames - idx)) / 32;
      int16_t sample = int16_t((tri * amp) >> 8);
      chunk[i * 2] = sample;
      chunk[i * 2 + 1] = sample;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, chunk, n * 2 * sizeof(int16_t), &written, 0);
    done += n;
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
#elif defined(ARDUINO) && defined(LILYGO_TDECK)
  if (!s_i2s_ready) {
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 8000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) == ESP_OK) {
      i2s_pin_config_t pins = {};
      pins.bck_io_num = 7;
      pins.ws_io_num = 5;
      pins.data_out_num = 6;
      pins.data_in_num = I2S_PIN_NO_CHANGE;
      i2s_set_pin(I2S_NUM_0, &pins);
      i2s_zero_dma_buffer(I2S_NUM_0);
      s_i2s_ready = true;
    }
  }
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
