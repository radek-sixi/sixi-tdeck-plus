// SPDX-License-Identifier: Apache-2.0
// app/core/AppStore.h — Observable application state store (zero MeshCore/Arduino/LVGL includes)
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Types.h"
#include "EventBus.h"
#include "RepeatRoamService.h"

namespace faro::core {

struct UiPrefs {
  Language language = Language::EN;
  bool     outdoor_high_contrast = true;  // Default: Google Material 3 Light UX
  uint8_t  brightness_pct = 85;
  uint8_t  kb_backlight = 128;
  uint16_t screen_timeout_secs = 60;
  bool     quiet_mode = false;
  bool     repeat_roam = true;
  bool     first_boot_done = true;
  bool     show_timestamps = true;
  bool     kid_mode = false;
  bool     sniffer_paused = false;
  bool     mention_only_filter = false;
  uint8_t  coord_format = 0;  // 0=WGS84, 1=SwissLV95, 2=UTM
  bool     search_active = false;
  char     search_query[32] = {0};
  char     custom_tags[3][10] = {"Family", "Friend", "Team"};
  char     clipboard[160] = {0};
  uint8_t  bot_mode = 0;             // 0=OFF, 1=PingBot, 2=AwayBot
  uint16_t bot_replies_sent = 0;
  char     away_msg[64] = "Away from rig (Alpine hike). Will reply soon.";
  bool     range_test_active = false;
  uint8_t  deadman_mode = 0;         // 0=OFF, 1=15m, 2=30m, 3=60m
  uint32_t deadman_remaining_secs = 1800;
  bool     deadman_triggered = false;
  bool     store_and_forward = true; // Re-send queued/failed DMs when the peer is heard again
  uint16_t sf_delivered_count = 0;
};

class AppStore {
public:
  static constexpr size_t kMaxContacts      = 64;   // active UI view / store capacity (up to 350 on device)
  static constexpr size_t kMaxChannels      = 40;
  static constexpr size_t kMaxMessages      = 256;
  static constexpr size_t kMaxDiscovered    = 16;
  static constexpr size_t kMaxCanned        = 12;

  static constexpr uint32_t kDirectAckTimeoutMs = 6000;
  static constexpr uint8_t  kMaxDirectRetries   = 3;

  AppStore();

  uint32_t version() const { return _version; }
  void bumpVersion() { ++_version; }

  // Event ingestion from MeshEventBus
  void applyEvent(const MeshEvent& ev, uint32_t now_ms = 0);

  // Periodic retry/timeout tick for outgoing DMs
  // Returns true if any message changed state (e.g. direct -> flood fallback or failed).
  // Messages that need another transmission are queued; drain them with takeResendRequest().
  bool tickMessageTimeouts(uint32_t now_ms);
  bool takeResendRequest(uint32_t& out_msg_id);
  MessageRecord* findMessageByIdMutable(uint32_t msg_id);

  // Contacts
  size_t contactCount() const;
  const ContactRecord* contacts() const { return _contacts; }
  ContactRecord* contactsMutable() { return _contacts; }
  const ContactRecord* findContactByPubKey(const uint8_t pub_key[32]) const;
  ContactRecord* findContactByPubKeyMutable(const uint8_t pub_key[32]);
  const ContactRecord* findContactByPrefix(const uint8_t prefix6[6]) const;
  ContactRecord* findContactByPrefixMutable(const uint8_t prefix6[6]);
  ContactRecord* upsertContact(const ContactRecord& c);
  bool removeContact(const uint8_t pub_key[32]);
  void clearContactUnread(const uint8_t prefix6[6]);
  uint32_t totalUnreadDm() const;

  // Channels
  size_t channelCount() const;
  const ChannelRecord* channels() const { return _channels; }
  ChannelRecord* channelsMutable() { return _channels; }
  const ChannelRecord* findChannel(uint8_t index) const;
  ChannelRecord* upsertChannel(const ChannelRecord& ch);
  bool removeChannel(uint8_t index);
  void clearChannelUnread(uint8_t index);
  uint32_t totalUnreadChannels() const;

  // Messages
  size_t messageCount() const { return _msg_count; }
  const MessageRecord& messageAt(size_t logical_idx) const;
  uint32_t appendMessage(const MessageRecord& msg);
  bool markAckReceived(uint32_t ack_crc);


  // Canned messages
  size_t cannedCount() const { return _canned_count; }
  const char* cannedAt(size_t idx) const;
  void setCannedMessages(const char* const* list, size_t count);

  // State sub-models
  RepeatRoamService& repeatRoam() { return _repeat_roam; }
  const RepeatRoamService& repeatRoam() const { return _repeat_roam; }

  TraceResult& lastTrace() { return _last_trace; }
  const TraceResult& lastTrace() const { return _last_trace; }

  GpsFix& gps() { return _gps; }
  const GpsFix& gps() const { return _gps; }

  RadioConfig& radio() { return _radio; }
  const RadioConfig& radio() const { return _radio; }

  // Radio region (sets the allowed sub-bands, power and duty limits). The Sixi edition stays on EU 868.
  RadioRegionId region() const { return _region; }
  const RadioRegion& regionInfo() const { return RegionPolicy::get(_region); }
  bool setRegion(RadioRegionId id);

  // Nodes that answered our last discover request
  size_t discoveredCount() const;
  const DiscoveredNode* discovered() const { return _discovered; }
  void clearDiscovered();

  // Answer to a request this device sent (status, telemetry, neighbours, ACL, login)
  void applyRemoteResponse(RemoteRequestKind kind, const uint8_t pub_key[32], const uint8_t* data, size_t len);

  // GPS track: adds a point while recording (skips points closer than 15 m); thins the track when full
  bool recordTrackPoint(double lat_deg, double lon_deg, float alt_m, uint32_t ts);

  SystemStatus& status() { return _status; }
  const SystemStatus& status() const { return _status; }

  UiPrefs& uiPrefs() { return _ui_prefs; }
  const UiPrefs& uiPrefs() const { return _ui_prefs; }

  static constexpr size_t kMaxRegions = 8;
  RegionRecord* regions() { return _regions; }
  const RegionRecord* regions() const { return _regions; }
  size_t regionCount() const {
    size_t n = 0;
    for (size_t i = 0; i < kMaxRegions; ++i) {
      if (_regions[i].valid) ++n;
    }
    return n;
  }

  static constexpr size_t kMaxPacketLogs = 24;
  void appendPacketLog(const RxPacketLogEntry& entry);
  void clearPacketLogs();
  size_t packetLogCount() const { return _pkt_log_count; }
  const RxPacketLogEntry& packetLogAt(size_t logical_idx) const;

  GpsTrackSummary& gpsTrack() { return _gps_track; }
  const GpsTrackSummary& gpsTrack() const { return _gps_track; }
  bool exportGpsTrackGpx(const char* filepath, char* out_saved_path = nullptr, size_t max_len = 0);

  static constexpr size_t kMaxRangeSamples = 32;
  void recordRangeSample(double lat_deg, double lon_deg, int8_t snr_q4, int16_t rssi, uint8_t hops, uint32_t ts = 0);
  void clearRangeSamples();
  size_t rangeSampleCount() const { return _range_sample_count; }
  const RangeTestSample* rangeSamples() const { return _range_samples; }
  void pushRfHistorySample(int16_t rssi_dbm, int16_t noise_dbm);

  // Export contacts to JSON
  bool exportContactsJson(const char* filepath, char* out_saved_path = nullptr, size_t max_len = 0);


  // Persistence. UI state = UI preferences, radio region, contact tags/notes/favourites/mutes, channel
  // mutes. Messages = the chat history. Files are written to <path>.tmp first, then renamed.
  bool saveUiState(const char* filepath) const;
  bool loadUiState(const char* filepath);
  bool saveMessages(const char* filepath) const;
  bool loadMessages(const char* filepath);
  void clearMessages();
  // Cheap fingerprints to decide when the files need writing
  uint32_t uiStateHash() const;
  uint32_t messagesHash() const;

  // Both parts in one file (desktop console)
  bool saveToFile(const char* filepath) const;
  bool loadFromFile(const char* filepath);

  bool hasPathHashCollision(uint8_t hash_byte) const {
    size_t cnt = 0;
    for (size_t i = 0; i < kMaxContacts; ++i) {
      if (_contacts[i].valid && _contacts[i].pub_key[0] == hash_byte) {
        if (++cnt > 1) return true;
      }
    }
    return false;
  }

  // MeshCore QR URI helpers (meshcore://contact/add & meshcore://channel/add)
  static bool formatContactUri(const ContactRecord& c, char* out_uri, size_t max_len);
  static bool parseContactUri(const char* uri, ContactRecord& out_c);
  static bool formatChannelUri(const ChannelRecord& ch, char* out_uri, size_t max_len);
  static bool parseChannelUri(const char* uri, ChannelRecord& out_ch);

  // TRACE hop list for a contact: its known path out (+ the contact itself when it repeats), then the
  // same hops back. Returns the byte length (0 = nothing to trace); flags gets the trace hash size code.
  static size_t buildTracePath(const uint8_t* out_path, uint8_t out_path_len, const uint8_t* target_pub,
                               bool target_repeats, uint8_t* path, size_t cap, uint8_t& flags);

  static void bytesToHex(const uint8_t* bytes, size_t len, char* out_hex, size_t max_len);
  static bool hexToBytes(const char* hex, uint8_t* out_bytes, size_t expected_len);

private:
  friend struct AppStoreCodec;  // persistence (AppStore.cpp)
  uint32_t _version = 1;
  ContactRecord _contacts[kMaxContacts] = {};
  ChannelRecord _channels[kMaxChannels] = {};
  MessageRecord _messages[kMaxMessages] = {};
  size_t _msg_head = 0;
  size_t _msg_count = 0;
  uint32_t _next_msg_id = 1;


  static constexpr size_t kMaxResendRequests = 8;
  uint32_t _resend_ids[kMaxResendRequests] = {};
  size_t   _resend_count = 0;
  void queueResend(uint32_t msg_id);

  char _canned[kMaxCanned][64] = {};
  size_t _canned_count = 0;

  RepeatRoamService _repeat_roam;
  TraceResult       _last_trace;
  GpsFix            _gps;
  RadioConfig       _radio;
  RadioRegionId     _region = RadioRegionId::EU868;
  DiscoveredNode    _discovered[kMaxDiscovered] = {};
  SystemStatus      _status;
  UiPrefs           _ui_prefs;
  RegionRecord      _regions[kMaxRegions] = {};
  GpsTrackSummary   _gps_track;
  RxPacketLogEntry  _pkt_logs[kMaxPacketLogs] = {};
  size_t            _pkt_log_head = 0;
  size_t            _pkt_log_count = 0;
  RangeTestSample   _range_samples[kMaxRangeSamples] = {};
  size_t            _range_sample_count = 0;
};

}  // namespace faro::core
