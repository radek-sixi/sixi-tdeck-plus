// SPDX-License-Identifier: Apache-2.0
// Trackball.cpp — LilyGO T-Deck Hall-effect trackball driver (UP=3, DOWN=15, LEFT=1, RIGHT=2, CLICK=0)
#ifdef ARDUINO
#include <Arduino.h>
#include "Trackball.h"

namespace faro::hal {

namespace {
volatile int16_t g_up_pulses = 0;
volatile int16_t g_down_pulses = 0;
volatile int16_t g_left_pulses = 0;
volatile int16_t g_right_pulses = 0;

void IRAM_ATTR isrUp()    { g_up_pulses++; }
void IRAM_ATTR isrDown()  { g_down_pulses++; }
void IRAM_ATTR isrLeft()  { g_left_pulses++; }
void IRAM_ATTR isrRight() { g_right_pulses++; }
}  // namespace

void Trackball::begin() {
#if defined(LILYGO_TDECK)
  pinMode(kPinUp, INPUT_PULLUP);
  pinMode(kPinDown, INPUT_PULLUP);
  pinMode(kPinLeft, INPUT_PULLUP);
  pinMode(kPinRight, INPUT_PULLUP);
  pinMode(kPinClick, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(kPinUp), isrUp, FALLING);
  attachInterrupt(digitalPinToInterrupt(kPinDown), isrDown, FALLING);
  attachInterrupt(digitalPinToInterrupt(kPinLeft), isrLeft, FALLING);
  attachInterrupt(digitalPinToInterrupt(kPinRight), isrRight, FALLING);
#endif
}

bool Trackball::isPressedRaw() const {
#if defined(LILYGO_TDECK)
  return digitalRead(kPinClick) == LOW;
#else
  return false;
#endif
}

void Trackball::poll(int16_t& dx, int16_t& dy, bool& clicked, bool& long_pressed) {
  noInterrupts();
  int16_t up = g_up_pulses;       g_up_pulses = 0;
  int16_t down = g_down_pulses;   g_down_pulses = 0;
  int16_t left = g_left_pulses;   g_left_pulses = 0;
  int16_t right = g_right_pulses; g_right_pulses = 0;
  interrupts();

  dx = right - left;
  dy = down - up;

  clicked = false;
  long_pressed = false;
  bool now_pressed = isPressedRaw();
  uint32_t now_ms = millis();

  if (now_pressed && !_was_pressed) {
    _press_start_ms = now_ms;
    _long_fired = false;
  } else if (now_pressed && _was_pressed) {
    if (!_long_fired && (now_ms - _press_start_ms >= 700)) {
      long_pressed = true;
      _long_fired = true;
    }
  } else if (!now_pressed && _was_pressed) {
    if (!_long_fired && (now_ms - _press_start_ms >= 25)) {
      clicked = true;
    }
  }
  _was_pressed = now_pressed;
  _held_ms = now_pressed ? now_ms - _press_start_ms : 0;
}

}  // namespace faro::hal
#endif
