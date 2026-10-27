// SPDX-License-Identifier: Apache-2.0
// PowerBoard.cpp — T-Deck Plus peripheral power (GPIO 10) & battery ADC (GPIO 4)
#include "PowerBoard.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#if defined(ARDUINO) && defined(LILYGO_TDECK)
// This T-Deck Plus revision never completes an external-channel conversion on
// the ESP32-S3 RTC ADC controller: adc1_get_raw() spins forever in
// adc_ll_rtc_convert_is_done() and trips the interrupt watchdog. The digital
// controller (DMA) on the same ADC1 channel works, so battery reads use it and
// the Arduino analog* entry points are overridden to keep every caller
// (TDeckBoard, telemetry, our own UI) away from the broken RTC path.
#include <driver/adc.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint8_t kBattAdcChannel = 3;  // ADC1 channel 3 = GPIO 4
constexpr uint32_t kRawCacheMs = 1000;
constexpr uint32_t kReadTimeoutMs = 50;

SemaphoreHandle_t s_adc_mutex = nullptr;
bool s_adc_ready = false;
int s_last_raw = -1;
uint32_t s_last_raw_ms = 0;

bool adcDigiBegin() {
  if (s_adc_ready) return true;
  if (s_adc_mutex == nullptr) {
    s_adc_mutex = xSemaphoreCreateMutex();
    if (s_adc_mutex == nullptr) return false;
  }

  adc_digi_init_config_t init = {};
  init.max_store_buf_size = 1024;
  init.conv_num_each_intr = 32;
  init.adc1_chan_mask = (1U << kBattAdcChannel);
  init.adc2_chan_mask = 0;
  if (adc_digi_initialize(&init) != ESP_OK) return false;

  adc_digi_pattern_config_t pattern = {};
  pattern.atten = ADC_ATTEN_DB_11;
  pattern.channel = kBattAdcChannel;
  // The digital-controller pattern uses 0 for ADC1 and 1 for ADC2. Passing
  // ADC_UNIT_1 (=1) here silently selects ADC2 and configure() rejects it.
  pattern.unit = 0;
  pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  adc_digi_configuration_t cfg = {};
  cfg.conv_limit_en = false;
  cfg.conv_limit_num = 0;
  cfg.pattern_num = 1;
  cfg.adc_pattern = &pattern;
  cfg.sample_freq_hz = 20000;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  if (adc_digi_controller_configure(&cfg) != ESP_OK) {
    adc_digi_deinitialize();
    return false;
  }

  s_adc_ready = true;
  return true;
}

// Caller must hold s_adc_mutex.
int adcDigiReadLocked() {
  if (!s_adc_ready) return -1;
  if (adc_digi_start() != ESP_OK) return -1;

  uint8_t buf[64];
  uint32_t len = 0;
  // The first DMA buffer can hold samples captured before start(); discard it.
  adc_digi_read_bytes(buf, sizeof(buf), &len, kReadTimeoutMs);
  len = 0;
  const esp_err_t err = adc_digi_read_bytes(buf, sizeof(buf), &len, kReadTimeoutMs);
  adc_digi_stop();

  if (err != ESP_OK || len < sizeof(adc_digi_output_data_t)) return -1;

  uint32_t acc = 0;
  int n = 0;
  for (uint32_t i = 0; i + sizeof(adc_digi_output_data_t) <= len;
       i += sizeof(adc_digi_output_data_t)) {
    const auto* d = reinterpret_cast<const adc_digi_output_data_t*>(buf + i);
    if (d->type2.unit == 0 && d->type2.channel == kBattAdcChannel) {
      acc += d->type2.data;
      ++n;
    }
  }
  return n > 0 ? int(acc / uint32_t(n)) : -1;
}

int adcBattRawCached() {
  const uint32_t now = millis();
  if (s_last_raw >= 0 && now - s_last_raw_ms < kRawCacheMs) return s_last_raw;

  if (!adcDigiBegin()) return s_last_raw;
  if (xSemaphoreTake(s_adc_mutex, pdMS_TO_TICKS(300)) != pdTRUE) return s_last_raw;
  const int raw = adcDigiReadLocked();
  xSemaphoreGive(s_adc_mutex);

  if (raw > 0) {
    s_last_raw = raw;
    s_last_raw_ms = now;
  }
  return s_last_raw;
}

}  // namespace

// Arduino core 2.0.17 exposes these as weak aliases, so strong definitions here
// replace them for the whole firmware.
extern "C" uint16_t analogRead(uint8_t pin) {
  if (pin != faro::hal::PowerBoard::kPinBattAdc) return 0;
  const int raw = adcBattRawCached();
  return raw > 0 ? uint16_t(raw) : 0;
}

extern "C" uint16_t analogReadRaw(uint8_t pin) { return analogRead(pin); }

extern "C" uint32_t analogReadMilliVolts(uint8_t pin) {
  const uint16_t raw = analogRead(pin);
  return (uint32_t(raw) * 3300U) / 4095U;
}

extern "C" void analogReadResolution(uint8_t) {}

extern "C" bool adcAttachPin(uint8_t pin) {
  return pin == faro::hal::PowerBoard::kPinBattAdc;
}

#endif  // ARDUINO && LILYGO_TDECK

namespace faro::hal {

void PowerBoard::begin() {
#ifdef ARDUINO
#if defined(LILYGO_TDECK)
  pinMode(kPinPowerOn, OUTPUT);
  digitalWrite(kPinPowerOn, HIGH);
  (void)adcBattRawCached();  // warm up the DMA path before the UI starts
#elif defined(THINKNODE_M9)
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);
#endif
#ifdef PIN_VBAT_READ
  pinMode(PIN_VBAT_READ, INPUT);
#else
#if !defined(LILYGO_TDECK)
  pinMode(kPinBattAdc, INPUT);
#endif
#endif
#if !defined(LILYGO_TDECK)
  analogReadResolution(12);
#endif
#endif
}

uint16_t PowerBoard::readBattMilliVolts() {
#ifdef ARDUINO
  const uint32_t now = millis();
  if (_last_mv > 0 && now - _last_read_ms < 2000U) return _last_mv;

  int raw = -1;
#if defined(LILYGO_TDECK)
  raw = adcBattRawCached();
#else
  uint32_t acc = 0;
#ifdef PIN_VBAT_READ
  const int adc_pin = PIN_VBAT_READ;
#else
  const int adc_pin = kPinBattAdc;
#endif
  for (int i = 0; i < 8; ++i) {
    acc += analogRead(adc_pin);
  }
  raw = int(acc / 8U);
#endif
  if (raw < 0) return _last_mv;

  // TDeckBoard formula: 2 * 3.3V * 1000 / 4096
  const uint16_t mv = uint16_t((uint32_t(raw) * 6600U) / 4096U);
  if (_last_mv > 0) {
    if (mv > _last_mv + 25 || mv >= 4185) {
      _charging = true;
    } else if (mv + 15 < _last_mv) {
      _charging = false;
    }
  }
  _last_mv = mv;
  _last_read_ms = now;
  return mv;
#else
  return 4050;
#endif
}

uint8_t PowerBoard::readBattPercent(uint16_t mv) const {
  if (mv <= 3300) return 0;
  if (mv >= 4200) return 100;
  // Piecewise LiPo discharge curve approximation
  if (mv >= 3700) {
    // 3700..4200 mV -> 20..100%
    return uint8_t(20 + ((uint32_t(mv - 3700) * 80U) / 500U));
  }
  // 3300..3700 mV -> 0..20%
  return uint8_t((uint32_t(mv - 3300) * 20U) / 400U);
}

}  // namespace faro::hal
