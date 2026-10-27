// SPDX-License-Identifier: Apache-2.0
// app/core/RadioRegions.h — Radio regions: allowed sub-bands, power and duty limits, community presets
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace faro::core {

// Antenna gain assumed when a power limit is given as e.r.p. or e.i.r.p.
#ifndef FARO_ANTENNA_GAIN_DBI
#define FARO_ANTENNA_GAIN_DBI 3.0f
#endif

// The ids are stored on the device (UI state file): append new regions, never renumber.
enum class RadioRegionId : uint8_t {
  EU868  = 0,  // EU / UK / CH, 869.400-869.650 MHz
  EU433  = 1,  // EU / UK, 433.050-434.790 MHz
  US915  = 2,  // USA / Canada, 902-928 MHz
  ANZ915 = 3,  // Australia / New Zealand, 915-928 MHz
  BR915  = 4,  // Brazil, 902-907.5 and 915-928 MHz
  _Count
};

enum class PowerRef : uint8_t { Erp, Eirp };

struct RegionBand {
  float low_mhz;
  float high_mhz;
};

struct RegionPreset {
  const char* name;
  float       freq_mhz;
  float       bw_khz;
  uint8_t     sf;
  uint8_t     cr;
  uint8_t     path_hash_bytes;  // 0 = keep the current path hash size
};

// Firmware ceilings for one region. They follow the national rules as far as MeshCore can enforce them
// (MeshCore limits its duty cycle only through the airtime factor, 0-9 = 100-10 %). They are not legal
// advice: the operator stays responsible for the rules of the country.
struct RadioRegion {
  RadioRegionId       id;
  const char*         code;                 // short label for buttons
  const char*         name;                 // long label
  RegionBand          bands[2];             // the whole channel (centre +/- BW/2) must fit one of them
  uint8_t             band_count;
  float               power_limit_dbm;      // in power_ref terms
  PowerRef            power_ref;
  float               conducted_limit_dbm;  // extra cap at the antenna port (99 = none)
  float               min_airtime_factor;   // duty = 1 / (1 + af): 9 -> 10 %, 0 -> no limit
  float               default_airtime_factor;
  const RegionPreset* presets;
  uint8_t             preset_count;
  const float*        repeat_freqs;         // client-repeat frequencies (upstream isValidClientRepeatFreq)
  uint8_t             repeat_count;
  double              map_lat;              // map centre without GPS fix or tiles
  double              map_lon;
  const char*         quick_tags[4];        // hashtag channels offered with one tap ("" = none)
  const char*         note;
};

// Community presets from the MeshCore app (api.meshcore.nz/api/v1/config), only those whose whole channel
// fits the region. "EU/CH Long Range" is Faro's own.
inline constexpr RegionPreset kPresetsEu868[] = {
  {"EU/UK/CH Narrow",  869.618f,  62.5f,  8, 8, 0},
  {"EU/UK Wide (old)", 869.525f, 250.0f, 11, 5, 0},
  {"EU/CH Long Range", 869.525f, 125.0f, 12, 8, 0},
  {"Czech Narrow",     869.432f,  62.5f,  7, 5, 2},
  {"Netherlands",      869.618f,  62.5f,  7, 5, 0},
  {"Hungary/Slovakia", 869.618f,  62.5f,  7, 5, 2},
  {"Portugal 868",     869.618f,  62.5f,  7, 6, 0},
  {"NL Limburg",       869.618f,  62.5f,  8, 8, 2},
};
inline constexpr RegionPreset kPresetsEu433[] = {
  {"EU433 Long Range", 433.650f, 250.0f, 11, 5, 0},
  {"EU433 Narrow",     433.650f,  62.5f,  8, 8, 0},
  {"Portugal 433",     433.375f,  62.5f,  9, 6, 0},
};
inline constexpr RegionPreset kPresetsUs915[] = {
  {"USA/Canada",       910.525f,  62.5f,  7, 5, 0},
  {"Canada (3-byte)",  910.525f,  62.5f,  7, 5, 3},
  {"USA SoCal",        927.875f,  62.5f,  7, 5, 3},
};
inline constexpr RegionPreset kPresetsAnz915[] = {
  {"Australia",        915.800f, 250.0f, 10, 5, 0},
  {"AU Narrow",        916.575f,  62.5f,  7, 7, 0},
  {"AU Mid",           915.075f, 125.0f,  9, 5, 0},
  {"AU SA/WA",         923.125f,  62.5f,  8, 8, 0},
  {"AU QLD",           923.125f,  62.5f,  8, 5, 0},
  {"NZ Narrow",        917.375f,  62.5f,  7, 5, 2},
  {"NZ Gisborne",      917.375f, 250.0f, 11, 5, 1},
};
inline constexpr RegionPreset kPresetsBr915[] = {
  {"Brazil",           923.125f,  62.5f,  8, 8, 0},
};

inline constexpr float kRepeatEu868[] = {869.495f, 869.525f, 869.618f};
inline constexpr float kRepeat915[] = {918.000f};

inline constexpr RadioRegion kRadioRegions[] = {
  {RadioRegionId::EU868, "EU 868", "EU/UK/CH 869.4-869.65 MHz",
   {{869.400f, 869.650f}, {0.0f, 0.0f}}, 1,
   27.0f, PowerRef::Erp, 99.0f, 9.0f, 9.0f,
   kPresetsEu868, sizeof(kPresetsEu868) / sizeof(kPresetsEu868[0]),
   kRepeatEu868, sizeof(kRepeatEu868) / sizeof(kRepeatEu868[0]),
   46.8182, 8.2275, {"#ch", "#eu", "#alps", "#test"},
   "CEPT ERC/REC 70-03: 500 mW e.r.p., 10 % duty"},
  {RadioRegionId::EU433, "EU 433", "EU/UK 433.05-434.79 MHz",
   {{433.050f, 434.790f}, {0.0f, 0.0f}}, 1,
   10.0f, PowerRef::Erp, 99.0f, 9.0f, 9.0f,
   kPresetsEu433, sizeof(kPresetsEu433) / sizeof(kPresetsEu433[0]),
   nullptr, 0,
   46.8182, 8.2275, {"#eu", "#433", "#test", ""},
   "433 MHz hardware only: 10 mW e.r.p., 10 % duty"},
  {RadioRegionId::US915, "US 915", "USA/Canada 902-928 MHz",
   {{902.000f, 928.000f}, {0.0f, 0.0f}}, 1,
   36.0f, PowerRef::Eirp, 30.0f, 0.0f, 1.0f,
   kPresetsUs915, sizeof(kPresetsUs915) / sizeof(kPresetsUs915[0]),
   kRepeat915, sizeof(kRepeat915) / sizeof(kRepeat915[0]),
   39.8283, -98.5795, {"#usa", "#canada", "#test", ""},
   "FCC 15.247 / RSS-247: 1 W conducted, 4 W e.i.r.p."},
  {RadioRegionId::ANZ915, "AU/NZ 915", "Australia/NZ 915-928 MHz",
   {{915.000f, 928.000f}, {0.0f, 0.0f}}, 1,
   30.0f, PowerRef::Eirp, 99.0f, 0.0f, 1.0f,
   kPresetsAnz915, sizeof(kPresetsAnz915) / sizeof(kPresetsAnz915[0]),
   kRepeat915, sizeof(kRepeat915) / sizeof(kRepeat915[0]),
   -25.2744, 133.7751, {"#aus", "#nz", "#test", ""},
   "ACMA LIPD / NZ GURL: 1 W e.i.r.p."},
  {RadioRegionId::BR915, "BR 915", "Brazil 902-907.5/915-928 MHz",
   {{902.000f, 907.500f}, {915.000f, 928.000f}}, 2,
   30.0f, PowerRef::Eirp, 30.0f, 0.0f, 1.0f,
   kPresetsBr915, sizeof(kPresetsBr915) / sizeof(kPresetsBr915[0]),
   kRepeat915, sizeof(kRepeat915) / sizeof(kRepeat915[0]),
   -14.2350, -51.9253, {"#brasil", "#test", "", ""},
   "Anatel: 1 W e.i.r.p. (Faro ceiling)"},
};
inline constexpr size_t kNumRadioRegions = sizeof(kRadioRegions) / sizeof(kRadioRegions[0]);
static_assert(kNumRadioRegions == size_t(RadioRegionId::_Count), "one table row per RadioRegionId");

struct RegionPolicy {
  static constexpr int8_t kRadioMaxDbm = 22;   // SX1262 / LR1110 PA limit
  static constexpr int8_t kRadioMinDbm = -9;
  static constexpr float  kMaxAirtimeFactor = 9.0f;  // upstream clamps the stored value to 0-9

  static const RadioRegion& get(RadioRegionId id) {
    const size_t i = size_t(id);
    return kRadioRegions[i < kNumRadioRegions ? i : 0];
  }

  static bool isKnown(uint8_t raw) { return raw < kNumRadioRegions; }

  static RadioRegionId next(RadioRegionId id) {
    return RadioRegionId((size_t(id) + 1) % kNumRadioRegions);
  }

  // The whole channel (centre +/- BW/2) inside one sub-band of the region; 0.5 kHz tolerance for floats.
  static bool channelInRegion(const RadioRegion& r, float freq_mhz, float bw_khz) {
    const float half = bw_khz / 2000.0f;
    for (uint8_t i = 0; i < r.band_count && i < 2; ++i) {
      if ((freq_mhz - half) >= r.bands[i].low_mhz - 0.0005f &&
          (freq_mhz + half) <= r.bands[i].high_mhz + 0.0005f) {
        return true;
      }
    }
    return false;
  }

  // Highest conducted power that keeps the region's e.r.p./e.i.r.p. limit with the given antenna.
  static int8_t maxConductedDbm(const RadioRegion& r, float antenna_gain_dbi = FARO_ANTENNA_GAIN_DBI) {
    float lim = r.power_limit_dbm - antenna_gain_dbi + (r.power_ref == PowerRef::Erp ? 2.15f : 0.0f);
    if (lim > r.conducted_limit_dbm) lim = r.conducted_limit_dbm;
    if (lim > kRadioMaxDbm) lim = kRadioMaxDbm;
    if (lim < kRadioMinDbm) lim = kRadioMinDbm;
    return int8_t(floorf(lim));
  }

  // Radiated power in the region's terms (e.r.p. = conducted + gain - 2.15 dB, e.i.r.p. = conducted + gain).
  static float radiatedDbm(const RadioRegion& r, int8_t conducted_dbm,
                           float antenna_gain_dbi = FARO_ANTENNA_GAIN_DBI) {
    return float(conducted_dbm) + antenna_gain_dbi - (r.power_ref == PowerRef::Erp ? 2.15f : 0.0f);
  }

  static const char* powerRefLabel(const RadioRegion& r) {
    return r.power_ref == PowerRef::Erp ? "ERP" : "EIRP";
  }

  static float dutyCyclePercent(float airtime_factor) {
    return 100.0f / (1.0f + (airtime_factor > 0.0f ? airtime_factor : 0.0f));
  }

  static bool isRepeatFrequency(const RadioRegion& r, float freq_mhz) {
    for (uint8_t i = 0; i < r.repeat_count; ++i) {
      if (fabsf(freq_mhz - r.repeat_freqs[i]) <= 0.005f) return true;
    }
    return false;
  }

  // Index of the preset matching the radio settings, -1 when none.
  static int findPreset(const RadioRegion& r, float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr) {
    for (uint8_t i = 0; i < r.preset_count; ++i) {
      const RegionPreset& p = r.presets[i];
      if (fabsf(freq_mhz - p.freq_mhz) < 0.0015f && fabsf(bw_khz - p.bw_khz) < 0.05f && sf == p.sf && cr == p.cr) {
        return int(i);
      }
    }
    return -1;
  }

  // Region for a channel: `preferred` when it fits there, else a region with a matching preset, else the
  // first region whose band holds the channel. False when no region allows it.
  static bool findRegionFor(float freq_mhz, float bw_khz, RadioRegionId preferred, RadioRegionId& out) {
    if (channelInRegion(get(preferred), freq_mhz, bw_khz)) {
      out = preferred;
      return true;
    }
    for (size_t i = 0; i < kNumRadioRegions; ++i) {
      const RadioRegion& r = kRadioRegions[i];
      for (uint8_t k = 0; k < r.preset_count; ++k) {
        if (fabsf(freq_mhz - r.presets[k].freq_mhz) < 0.0015f && channelInRegion(r, freq_mhz, bw_khz)) {
          out = r.id;
          return true;
        }
      }
    }
    for (size_t i = 0; i < kNumRadioRegions; ++i) {
      if (channelInRegion(kRadioRegions[i], freq_mhz, bw_khz)) {
        out = kRadioRegions[i].id;
        return true;
      }
    }
    return false;
  }
};

}  // namespace faro::core
