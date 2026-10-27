// SPDX-License-Identifier: Apache-2.0
// app/core/Types.h — Pure C++20 domain value types (zero MeshCore/Arduino/LVGL includes)
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "RadioRegions.h"

namespace faro::core {

enum class NodeRole : uint8_t {
  Unknown    = 0,
  ChatNode   = 1,
  Repeater   = 2,
  RoomServer = 3,
  Sensor     = 4
};

enum class DeliveryState : uint8_t {
  Queued       = 0,
  SentDirect   = 1,
  SentFlood    = 2,
  DeliveredAck = 3,
  Failed       = 4
};

enum class Language : uint8_t {
  EN = 0,
  IT = 1,
  DE = 2,
  FR = 3
};

struct ContactRecord {
  uint8_t  pub_key[32] = {0};
  char     name[32] = {0};
  NodeRole role = NodeRole::ChatNode;
  uint8_t  flags = 0;
  uint8_t  out_path_len = 0xFF;  // 0xFF = flood, 0..64 = direct hop count
  uint8_t  out_path[64] = {0};
  uint32_t last_advert_ts = 0;
  uint32_t lastmod_ts = 0;
  int32_t  gps_lat_e6 = 0;
  int32_t  gps_lon_e6 = 0;
  int8_t   last_snr_q4 = 0;      // SNR * 4 (quarter-dB)
  int16_t  last_rssi = -100;
  uint16_t unread_count = 0;
  uint8_t  tag_mask = 0;         // Bit 0=Tag A (Family), Bit 1=Tag B (Friend), Bit 2=Tag C (Team)
  char     notes[64] = {0};      // Local free-form notes (Ripple v7.14+)
  uint16_t remote_batt_mv = 0;   // Last telemetry battery voltage
  int8_t   remote_temp_c = 0;    // Last telemetry temperature
  bool     online = false;       // Set when telemetry / status response received
  bool     favourite = false;
  bool     muted = false;
  bool     valid = false;

  bool hasPrefix(const uint8_t prefix6[6]) const {
    return valid && memcmp(pub_key, prefix6, 6) == 0;
  }
  bool hasGps() const {
    return gps_lat_e6 != 0 || gps_lon_e6 != 0;
  }
  double latDeg() const { return double(gps_lat_e6) / 1000000.0; }
  double lonDeg() const { return double(gps_lon_e6) / 1000000.0; }
};

struct ChannelRecord {
  uint8_t  index = 0;
  char     name[32] = {0};
  uint8_t  secret[32] = {0};
  uint8_t  secret_len = 16;      // 16 (AES-128) or 32 (AES-256)
  char     scope_name[24] = "*"; // Flood region scope (Ripple v8.2+)
  bool     is_hashtag = false;
  uint16_t unread_count = 0;
  bool     muted = false;
  bool     valid = false;
};

struct MessageRecord {
  uint32_t      id = 0;
  bool          is_channel = false;
  uint8_t       channel_index = 0;
  uint8_t       peer_prefix[6] = {0};
  char          sender_name[32] = {0};
  char          text[160] = {0};
  uint32_t      timestamp = 0;
  uint32_t      sent_ms = 0;
  uint32_t      ack_crc = 0;
  DeliveryState delivery_state = DeliveryState::Queued;
  uint8_t       retry_count = 0;
  uint8_t       hops = 0;
  uint8_t       repeats_heard = 0;  // Group message repeat confirmations (Ripple v7.12+)
  uint8_t       heard_by_hashes[4] = {0};
  int8_t        snr_q4 = 0;
  int16_t       rssi = -100;
  bool          outgoing = false;
  bool          valid = false;
};

struct RegionRecord {
  char    name[24] = {0};
  float   freq_mhz = 0.0f;       // 0 = plain scope region; >0 = custom/off-grid frequency
  float   bw_khz = 62.5f;
  uint8_t sf = 8;
  uint8_t cr = 8;
  bool    offgrid_repeat = false;
  bool    is_home = false;
  bool    valid = false;
};

struct GpsTrackPoint {
  double   lat_deg = 0.0;
  double   lon_deg = 0.0;
  float    alt_m = 0.0f;
  uint32_t timestamp = 0;
};

struct GpsTrackSummary {
  char          name[32] = "track-active.gpx";
  GpsTrackPoint points[64] = {};
  size_t        point_count = 0;
  double        distance_m = 0.0;
  bool          recording = false;
};

struct RepeaterCandidate {
  uint8_t  pub_key[32] = {0};
  char     name[32] = {0};
  int8_t   snr_q4 = 0;
  int16_t  rssi = -110;
  uint32_t last_heard_ms = 0;
  int32_t  lat_e6 = 0;
  int32_t  lon_e6 = 0;
  bool     is_preferred = false;
  bool     valid = false;
};

struct TraceHop {
  uint8_t hash = 0;
  int8_t  snr_q4 = 0;
};

struct TraceResult {
  uint32_t tag = 0;
  uint32_t auth_code = 0;
  uint8_t  flags = 0;
  uint8_t  hop_count = 0;
  TraceHop hops[16] = {};
  uint32_t timestamp = 0;
  bool     valid = false;
};

struct GpsFix {
  bool     enabled = true;
  bool     valid = false;
  double   lat_deg = 0.0;
  double   lon_deg = 0.0;
  double   alt_m = 0.0;
  uint8_t  satellites = 0;
  float    speed_kmh = 0.0f;
  float    course_deg = 0.0f;
  uint32_t baud_rate = 38400;
  uint32_t nmea_sentences = 0;
  uint32_t last_fix_ts = 0;
};

// Radio limits enforced in every edition: the selected region (RadioRegions.h) sets the allowed
// sub-bands (whole channel = centre +/- BW/2), the e.r.p./e.i.r.p. ceiling and the duty cycle
// (MeshCore: duty = 1 / (1 + airtime_factor)). Without an explicit region the EU 868 rules apply
// (869.400-869.650 MHz, 27 dBm e.r.p., 10 %).

// The EU 868 band, kept for callers that predate regions.
struct RadioBandPolicy {
  static constexpr float kBandLowMhz       = 869.400f;
  static constexpr float kBandHighMhz      = 869.650f;
  static constexpr float kMaxErpDbm        = 27.0f;
  static constexpr float kMinAirtimeFactor = 9.0f;   // 10 % duty cycle
  static constexpr int8_t kRadioMaxDbm     = RegionPolicy::kRadioMaxDbm;
  static constexpr int8_t kRadioMinDbm     = RegionPolicy::kRadioMinDbm;

  static bool channelInBand(float freq_mhz, float bw_khz) {
    return RegionPolicy::channelInRegion(RegionPolicy::get(RadioRegionId::EU868), freq_mhz, bw_khz);
  }

  static int8_t maxConductedDbm(float antenna_gain_dbi = FARO_ANTENNA_GAIN_DBI) {
    return RegionPolicy::maxConductedDbm(RegionPolicy::get(RadioRegionId::EU868), antenna_gain_dbi);
  }

  static float erpDbm(int8_t conducted_dbm, float antenna_gain_dbi = FARO_ANTENNA_GAIN_DBI) {
    return float(conducted_dbm) + antenna_gain_dbi - 2.15f;
  }

  static float dutyCyclePercent(float airtime_factor) {
    return RegionPolicy::dutyCyclePercent(airtime_factor);
  }
};

struct RadioConfig {
  float   freq_mhz = 869.618f;
  float   bw_khz = 62.5f;
  uint8_t sf = 8;
  uint8_t cr = 8;
  int8_t  tx_power_dbm = 22;
  bool    rx_boosted_gain = true;
  bool    client_repeat = false;
  uint8_t path_hash_mode = 0;
  float   airtime_factor = RadioBandPolicy::kMinAirtimeFactor;

  // ADR-014: exact bandwidth whitelist (LR1110 has no sub-62.5 kHz LoRa bandwidths)
  static bool isValidSx1262Bandwidth(float bw) {
#if defined(FARO_RADIO_LR1110)
    static constexpr float kValidBws[] = {62.5f, 125.0f, 250.0f, 500.0f};
#else
    static constexpr float kValidBws[] = {
      7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f
    };
#endif
    for (float v : kValidBws) {
      if (fabsf(bw - v) <= 0.01f) return true;
    }
    return false;
  }

  // Centre frequency must leave room for at least the narrowest bandwidth.
  static bool isValidFrequencyBand(float f, const RadioRegion& region = RegionPolicy::get(RadioRegionId::EU868)) {
    return RegionPolicy::channelInRegion(region, f, 7.8f);
  }

  static bool isAllowedRepeatFrequency(float f, const RadioRegion& region = RegionPolicy::get(RadioRegionId::EU868)) {
    return RegionPolicy::isRepeatFrequency(region, f);
  }

  static bool isValid(const RadioConfig& cfg, const RadioRegion& region = RegionPolicy::get(RadioRegionId::EU868)) {
    if (!isValidSx1262Bandwidth(cfg.bw_khz)) return false;
    if (cfg.sf < 5 || cfg.sf > 12) return false;
    if (cfg.cr < 5 || cfg.cr > 8) return false;
    if (!RegionPolicy::channelInRegion(region, cfg.freq_mhz, cfg.bw_khz)) return false;
    if (cfg.tx_power_dbm < RegionPolicy::kRadioMinDbm ||
        cfg.tx_power_dbm > RegionPolicy::maxConductedDbm(region)) return false;
    if (!(cfg.airtime_factor >= region.min_airtime_factor)) return false;
    if (cfg.client_repeat && !isAllowedRepeatFrequency(cfg.freq_mhz, region)) return false;
    return true;
  }

  // Clamp what can be clamped (power, duty, repeat); band/BW/SF/CR are still checked by isValid().
  static RadioConfig sanitized(const RadioConfig& in,
                               const RadioRegion& region = RegionPolicy::get(RadioRegionId::EU868)) {
    RadioConfig out = in;
    const int8_t max_dbm = RegionPolicy::maxConductedDbm(region);
    if (out.tx_power_dbm > max_dbm) out.tx_power_dbm = max_dbm;
    if (out.tx_power_dbm < RegionPolicy::kRadioMinDbm) out.tx_power_dbm = RegionPolicy::kRadioMinDbm;
    if (!(out.airtime_factor >= region.min_airtime_factor)) out.airtime_factor = region.min_airtime_factor;
    if (out.airtime_factor > RegionPolicy::kMaxAirtimeFactor) out.airtime_factor = RegionPolicy::kMaxAirtimeFactor;
    if (out.client_repeat && !isAllowedRepeatFrequency(out.freq_mhz, region)) out.client_repeat = false;
    return out;
  }

  // Settings to apply when the user picks a region: its first preset, the highest allowed power and the
  // region's default duty cycle. `path_hash_bytes` gets the preset's path hash size (0 = keep).
  static RadioConfig regionDefault(const RadioRegion& region, const RadioConfig& current, uint8_t& path_hash_bytes) {
    RadioConfig out = current;
    const RegionPreset& p = region.presets[0];
    out.freq_mhz = p.freq_mhz;
    out.bw_khz = p.bw_khz;
    out.sf = p.sf;
    out.cr = p.cr;
    out.tx_power_dbm = RegionPolicy::maxConductedDbm(region);
    out.airtime_factor = region.default_airtime_factor;
    out.client_repeat = false;
    path_hash_bytes = p.path_hash_bytes;
    return out;
  }
};


struct RxPacketLogEntry {
  uint32_t timestamp_ms = 0;
  int8_t   snr_q4 = 0;
  int16_t  rssi = -100;
  uint8_t  hops = 0;
  uint16_t len_bytes = 0;
  char     type_label[12] = "PKT";
  char     summary[64] = {0};
  bool     valid = false;
};

struct RangeTestSample {
  double   lat_deg = 0.0;
  double   lon_deg = 0.0;
  int8_t   snr_q4 = 0;
  int16_t  rssi = -100;
  uint8_t  hops = 0;
  uint32_t timestamp = 0;
  bool     valid = false;
};

struct SystemStatus {
  char     node_name[32] = "Faro T-Deck";
  uint8_t  self_pubkey[32] = {0};
  uint16_t batt_mv = 4080;
  uint8_t  batt_pct = 88;
  bool     charging = false;
  bool     low_batt_warn = false;
  bool     radio_ok = true;
  bool     ble_connected = false;
  uint32_t ble_pin = 123456;
  bool     wifi_enabled = true;
  bool     wifi_connected = false;
  char     wifi_ssid[33] = {0};
  char     wifi_pwd[65] = {0};
  char     wifi_ip[16] = "0.0.0.0";
  uint16_t wifi_port = 5000;
  bool     sd_mounted = false;
  uint32_t sd_used_mb = 0;
  uint32_t sd_total_mb = 0;
  uint32_t uptime_secs = 0;
  uint32_t free_heap = 140000;
  uint32_t free_psram = 8000000;
  int16_t  noise_floor_dbm = -114;
  int16_t  last_rssi = -82;
  int8_t   last_snr_q4 = 32;
  uint32_t tx_packets = 0;
  uint32_t rx_packets = 0;
  uint32_t flood_tx = 0;
  uint32_t direct_tx = 0;
  uint16_t queue_size = 0;
  uint16_t last_loop_ms = 0;
  uint16_t max_loop_ms = 0;
  char     active_scope_name[31] = "*";
  uint16_t duty_cycle_permille = 0;
  bool     duty_cycle_throttled = false;

  // Extended MeshCore & Ripple UI settings state
  uint8_t  telemetry_perm = 2;       // 0=Deny, 1=Contacts, 2=Allow All (Base Telemetry)
  uint8_t  telemetry_loc_perm = 2;   // 0=Deny, 1=Contacts, 2=Allow All (Location Telemetry)
  uint8_t  telemetry_env_perm = 2;   // 0=Deny, 1=Contacts, 2=Allow All (Environment Telemetry)
  bool     auto_advert = true;       // Broadcast on boot + every 3h
  uint8_t  loc_policy = 2;           // 0=None, 1=Manual, 2=GPS
  float    rx_delay = 0.0f;
  float    tx_delay_factor = 0.5f;
  float    direct_tx_delay_factor = 0.2f;
  uint8_t  interference_db = 14;
  uint8_t  agc_reset_sec = 20;
  bool     double_acks = true;
  uint8_t  multi_acks = 1;           // 0=Off, 1=1x Extra, 2=2x Extra
  bool     cad_enabled = false;      // SX1262 Channel Activity Detection
  uint8_t  path_hash_bytes = 1;      // 1, 2, or 3 bytes
  bool     auto_add_contacts = true;
  uint8_t  auto_add_max_hops = 0;    // 0=All, 1=Direct, 2=2h, 3=3h, 5=5h
  int8_t   tz_offset_hours = 2;      // CEST (+2)
  double   manual_lat_deg = 0.0;      // 0/0 = no manual position set
  double   manual_lon_deg = 0.0;
  char     sd_backup_status[96] = "Ready: tap Export (overwrites SD) or Import.";
  char     radio_notice[96] = {0};   // set when stored or external settings were brought inside the region rules
  char     last_cli_reply[256] = "Ready. Type 'help', 'ver', 'get wifi.ip', 'clock'...";
  char     remote_admin_reply[256] = "Select Login, Get Stats, Telemetry, Neighbors, or ACL.";
  char     custom_vars_str[128] = "gps:1,gps_interval:60";
  char     conn_target[64] = "192.168.4.1:5000";
  char     last_gpx_path[96] = {0};
  char     last_rangetest_csv[96] = "/tmp/faro_rangetest.csv";
  int16_t  rssi_history[16] = {-95, -92, -88, -85, -83, -80, -84, -82, -79, -81, -83, -85, -80, -78, -81, -82};
  int16_t  noise_history[16] = {-115, -114, -115, -113, -114, -114, -115, -114, -113, -114, -115, -114, -114, -113, -114, -114};
  uint8_t  history_head = 0;
  char     last_json_export[96] = {0};
  bool     remote_logged_in = false;
  bool     auto_reconnect = true;
};

struct FaroSdSettingsSnapshot {
  uint32_t    magic = 0x46534431U;  // "FSD1"
  char        node_name[32] = "Faro T-Deck";
  RadioConfig radio{};
  uint8_t     telemetry_perm = 2;
  uint8_t     telemetry_loc_perm = 2;
  uint8_t     telemetry_env_perm = 2;
  uint8_t     loc_policy = 2;
  bool        auto_advert = true;
  bool        auto_add_contacts = true;
  uint8_t     auto_add_max_hops = 0;
  uint8_t     path_hash_bytes = 1;
  uint8_t     multi_acks = 1;
  bool        cad_enabled = false;
  float       rx_delay = 0.0f;
  float       tx_delay_factor = 0.5f;
  float       direct_tx_delay_factor = 0.2f;
  uint8_t     interference_db = 14;
  uint8_t     agc_reset_sec = 20;
  uint32_t    ble_pin = 123456;
  bool        wifi_enabled = true;
  char        wifi_ssid[33] = {0};
  char        wifi_pwd[65] = {0};
  bool        gps_enabled = true;
  uint32_t    gps_baud = 38400;
  int8_t      tz_offset_hours = 2;
  char        active_scope_name[31] = "*";
  double      manual_lat_deg = 0.0;
  double      manual_lon_deg = 0.0;
  uint8_t     language = 0;
  bool        outdoor_high_contrast = true;
  uint16_t    screen_timeout_secs = 60;
  bool        quiet_mode = false;
  uint8_t     coord_format = 0;
  bool        store_and_forward = true;
  uint8_t     bot_mode = 0;
  bool        kid_mode = false;
};

}  // namespace faro::core
