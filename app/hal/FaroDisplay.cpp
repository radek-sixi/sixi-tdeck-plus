// SPDX-License-Identifier: Apache-2.0
// FaroDisplay.cpp — T-Deck Plus ST7789 320x240 IPS display driver & LVGL flush backend (ADR-008)
#ifdef ARDUINO
#include <Arduino.h>
#include "FaroDisplay.h"

#ifdef FARO_HEADLESS
ColorVal UIColor::window_bkg = 0;
struct FaroDisplay::Impl {};
FaroDisplay::FaroDisplay() : DisplayDriver(0, 0), _impl(nullptr) {}
FaroDisplay::~FaroDisplay() {}
// No screen: upstream main.cpp then uses the fixed BLE PIN (BLE_PIN_CODE) instead of a random one
// that nobody could read
bool FaroDisplay::begin() { return false; }
void FaroDisplay::setBacklight(uint8_t percent) { _backlight_pct = percent; }
void FaroDisplay::turnOn() { _on = true; }
void FaroDisplay::turnOff() { _on = false; }
void FaroDisplay::clear() {}
void FaroDisplay::startFrame(ColorVal) {}
void FaroDisplay::setTextSize(int) {}
void FaroDisplay::setColor(ColorVal) {}
void FaroDisplay::setCursor(int, int) {}
void FaroDisplay::print(const char*) {}
void FaroDisplay::fillRect(int, int, int, int) {}
void FaroDisplay::drawRect(int, int, int, int) {}
void FaroDisplay::drawXbm(int, int, const uint8_t*, int, int) {}
uint16_t FaroDisplay::getTextWidth(const char*) { return 0; }
void FaroDisplay::endFrame() {}
void FaroDisplay::flush(int, int, int, int, const uint16_t*) {}
void FaroDisplay::reinitSpi() {}
#else
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <driver/gpio.h>

#if defined(THINKNODE_M9)
ColorVal UIColor::window_bkg = 0xFFFF;
#endif

#ifndef PIN_TFT_LEDA_CTL
#define PIN_TFT_LEDA_CTL 42
#endif
#if defined(THINKNODE_M9)
#ifndef PIN_TFT_CS
#define PIN_TFT_CS 16
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC 15
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST 14
#endif
#ifndef PIN_TFT_SCL
#define PIN_TFT_SCL 40
#endif
#ifndef PIN_TFT_SDA
#define PIN_TFT_SDA 47
#endif
#define FARO_SD_CS_PIN 48
#else
#define FARO_SD_CS_PIN 39
#endif

struct FaroDisplay::Impl {
  // Share FSPI (`SPI`) with MeshCore LoRa radio (`target.cpp`) and MicroSD (`SdCard.cpp`)
  // on SCLK=40, MISO=38, MOSI=41 so all three SPI peripherals stay attached to FSPI in
  // the ESP32-S3 GPIO matrix and share the same FreeRTOS SPI bus lock.
  SPIClass& spi{SPI};
  Adafruit_ST7789 tft{&spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST};
};

FaroDisplay::FaroDisplay() : DisplayDriver(320, 240), _impl(new Impl()) {}

FaroDisplay::~FaroDisplay() {
  delete _impl;
}

bool FaroDisplay::begin() {
  // Ensure LoRa NSS and SD CS are deasserted on the shared SPI pins
#ifdef P_LORA_NSS
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
#endif
  pinMode(FARO_SD_CS_PIN, OUTPUT);
  digitalWrite(FARO_SD_CS_PIN, HIGH);

#ifdef P_LORA_MISO
  _impl->spi.begin(PIN_TFT_SCL, P_LORA_MISO, PIN_TFT_SDA, -1);
#else
  _impl->spi.begin(PIN_TFT_SCL, 38, PIN_TFT_SDA, -1);
#endif
  _impl->tft.init(240, 320);
  _impl->tft.setRotation(3);
  _impl->tft.setSPISpeed(40000000);
  _impl->tft.fillScreen(ST77XX_BLACK);
  _impl->tft.setTextColor(ST77XX_WHITE);
  _impl->tft.setTextSize(2);
  // Restore FSPI hardware clock to 2 MHz so RadioLib's separate SPIClass instance
  // in target.cpp (which caches _freq=2000000) never transmits to SX1262 at 40 MHz.
  _impl->spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  _impl->spi.endTransaction();

  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  turnOn();
  return true;
}

void FaroDisplay::reinitSpi() {
  if (!_impl) return;
#ifdef P_LORA_NSS
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
#endif
  pinMode(FARO_SD_CS_PIN, OUTPUT);
  digitalWrite(FARO_SD_CS_PIN, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_DC, HIGH);
#ifdef P_LORA_MISO
  pinMode(P_LORA_MISO, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)P_LORA_MISO);
#endif
  _impl->tft.setSPISpeed(40000000);
  _impl->tft.setRotation(3);
  _impl->spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  _impl->spi.endTransaction();
}

#if !defined(THINKNODE_M9)
namespace {
// LilyGo T-Deck Plus uses an AW9364 1-wire 16-step pulse-dimming LED driver on GPIO 42.
// Never feed continuous PWM (analogWrite) into AW9364, as LOW > 0.5ms triggers shutdown/latch-off.
void applyTDeckAw9364Backlight(uint8_t percent) {
  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  if (percent == 0) {
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    delay(3);  // > 2.5ms shutdown reset
    return;
  }
  // Reset AW9364 internal counter via 3ms LOW, then pull HIGH -> wakes at Step 16/16 (100% brightness)
  digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  delay(3);
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
  delayMicroseconds(35);

  if (percent >= 80) {
    // Full brightness (Step 16/16): keep PIN_TFT_LEDA_CTL steady HIGH (0 pulses)
    return;
  }

  // Map 1..79% to AW9364 steps 2..15 (each 2us LOW->HIGH pulse decrements by 1/16)
  int step = (int(percent) * 16 + 99) / 100;
  if (step < 2) step = 2;
  if (step > 15) step = 15;
  int pulses = 16 - step;
  for (int i = 0; i < pulses; ++i) {
    noInterrupts();
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    interrupts();
    delayMicroseconds(5);
  }
}
}  // namespace
#endif

void FaroDisplay::setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  _backlight_pct = percent;
  if (_on) {
#if defined(THINKNODE_M9)
    uint32_t duty = (uint32_t(percent) * 255U) / 100U;
    analogWrite(PIN_TFT_LEDA_CTL, 255U - duty);
#else
    applyTDeckAw9364Backlight(percent);
#endif
  }
}

void FaroDisplay::turnOn() {
  _on = true;
  uint8_t pct = _backlight_pct ? _backlight_pct : 100;
#if defined(THINKNODE_M9)
  uint32_t duty = (uint32_t(pct) * 255U) / 100U;
  analogWrite(PIN_TFT_LEDA_CTL, 255U - duty);
#else
  applyTDeckAw9364Backlight(pct);
#endif
  if (_impl) {
    _impl->tft.enableSleep(false);  // wake panel (SLPOUT); the next flush redraws
    delay(5);
    _impl->tft.enableDisplay(true);
  }
}

void FaroDisplay::turnOff() {
  _on = false;
#if defined(THINKNODE_M9)
  analogWrite(PIN_TFT_LEDA_CTL, 255);
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
#else
  applyTDeckAw9364Backlight(0);
#endif
  if (_impl) _impl->tft.enableSleep(true);  // panel sleep (SLPIN): backlight off + controller idle
}

void FaroDisplay::clear() {
  if (_impl) _impl->tft.fillScreen(ST77XX_BLACK);
}

void FaroDisplay::startFrame(ColorVal) {
  if (_impl) _impl->tft.fillScreen(ST77XX_BLACK);
}

void FaroDisplay::setTextSize(int sz) {
  if (_impl) _impl->tft.setTextSize(sz > 0 ? sz : 1);
}

void FaroDisplay::setColor(ColorVal) {
  if (_impl) _impl->tft.setTextColor(ST77XX_WHITE);
}

void FaroDisplay::setCursor(int x, int y) {
  if (_impl) _impl->tft.setCursor(x, y);
}

void FaroDisplay::print(const char* str) {
  if (_impl && str) _impl->tft.print(str);
}

void FaroDisplay::fillRect(int x, int y, int w, int h) {
  if (_impl) _impl->tft.fillRect(x, y, w, h, ST77XX_WHITE);
}

void FaroDisplay::drawRect(int x, int y, int w, int h) {
  if (_impl) _impl->tft.drawRect(x, y, w, h, ST77XX_WHITE);
}

void FaroDisplay::drawXbm(int, int, const uint8_t*, int, int) {}

uint16_t FaroDisplay::getTextWidth(const char* str) {
  if (!str) return 0;
  return uint16_t(strlen(str) * 6);
}

void FaroDisplay::endFrame() {
  if (_impl) {
    _impl->spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    _impl->spi.endTransaction();
  }
}

void FaroDisplay::flush(int x, int y, int w, int h, const uint16_t* px) {
  if (!_impl || !px || w <= 0 || h <= 0) return;
  auto& t = _impl->tft;
  t.startWrite();
  t.setAddrWindow(x, y, w, h);
  t.writePixels(const_cast<uint16_t*>(px), uint32_t(w) * uint32_t(h));
  t.endWrite();
  // Restore FSPI hardware clock to 2 MHz for RadioLib SX1262 after 40 MHz TFT burst
  _impl->spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  _impl->spi.endTransaction();
}
#endif  // FARO_HEADLESS
#endif  // ARDUINO
