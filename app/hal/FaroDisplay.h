// SPDX-License-Identifier: Apache-2.0
// FaroDisplay.h — T-Deck Plus ST7789 320x240 IPS display driver & LVGL flush backend (ADR-008)
#pragma once

// Force-included everywhere via -include FaroDisplay.h: only define the class where MeshCore
// headers are reachable (project TUs: main.cpp, target.cpp, app/*). Library TUs see nothing.
#if defined(__cplusplus) && __has_include(<helpers/ui/DisplayDriver.h>)
#include <stdint.h>
#include <helpers/ui/DisplayDriver.h>

class FaroDisplay : public DisplayDriver {
public:
  FaroDisplay();
  ~FaroDisplay();

  bool begin();
  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;

  void setBacklight(uint8_t percent);
  uint8_t getBacklight() const { return _backlight_pct; }

  // DisplayDriver legacy interface (used by upstream main.cpp boot splash before UITask::begin)
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

  // Synchronous RGB565 block flush for LVGL 9 (ADR-007, ADR-008)
  void flush(int x, int y, int w, int h, const uint16_t* px);
  void reinitSpi();

private:
  struct Impl;
  Impl* _impl;
  bool _on = false;
  uint8_t _backlight_pct = 85;
};
#endif
