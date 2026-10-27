// SPDX-License-Identifier: Apache-2.0
// app/platform/DeviceMeshActions.h — Real ESP32-S3 MeshCore binding
#pragma once

#ifdef ARDUINO
#include <MeshCore.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include "MyMesh.h"
#include "NodePrefs.h"

#include "core/MeshActions.h"
#include "core/AppStore.h"
#include "core/EventBus.h"
#include "hal/SdCard.h"

namespace faro::platform {

class DeviceMeshActions : public core::IMeshActions {
public:
  DeviceMeshActions(core::AppStore& store, core::MeshEventBus& bus);

  // Reads MeshCore's state into the store. Call startRadioPolicy() once the saved UI state is loaded.
  void begin(mesh::MainBoard* board, MultiSerialInterface* ifaces,
             SensorManager* sensors, NodePrefs* prefs, hal::SdCard* sd = nullptr);
  // Picks the region (saved one, else the one the stored radio settings fit) and brings the radio
  // settings inside its rules.
  void startRadioPolicy(bool region_was_saved);
  void pollTelemetry(uint32_t now_ms, uint16_t batt_mv, uint8_t batt_pct,
                     bool charging, bool sd_mounted, uint32_t sd_used_mb, uint32_t sd_total_mb,
                     uint32_t nmea_sentences);
  // Radio settings changed outside Faro (phone app, TCP client, CLI): accept them when they fit a
  // region (the region follows), clamp power / duty, otherwise restore the last accepted settings.
  void checkExternalRadioChange(const char* source);

  // Listener hooks (UITask). claimResponse() returns true for answers to Faro's own requests.
  bool claimResponse(const ContactInfo& contact, uint32_t tag, const uint8_t* data, uint8_t len);
  void onControlData(const mesh::Packet* pkt);

  // IMeshActions implementation
  bool sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                         uint32_t& out_ack_crc, core::DeliveryState& out_state) override;
  bool sendChannelMessage(uint8_t channel_index, const char* text) override;
  bool resendDirectMessage(uint32_t msg_id) override;
  bool sendZeroHopAdvert() override;
  bool sendFloodAdvert() override;
  void syncStore() override;
  bool addOrUpdateContact(const core::ContactRecord& contact) override;
  bool removeContact(const uint8_t pub_key[32]) override;
  bool resetContactPath(const uint8_t pub_key[32]) override;
  bool setPreferredRepeater(const uint8_t repeater_pubkey[32]) override;
  bool addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) override;
  bool addHashtagChannel(const char* hashtag_name) override;
  bool removeChannel(uint8_t channel_index) override;
  bool sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) override;
  bool requestTelemetry(const uint8_t target_pubkey[32]) override;
  bool setRadioConfig(const core::RadioConfig& cfg) override;
  bool setNodeName(const char* name) override;
  bool setWifiConfig(bool enabled, const char* ssid, const char* password) override;
  bool setBlePin(uint32_t pin) override;
  bool setRepeatEnabled(bool enabled) override;
  bool setDefaultScope(const char* scope_name) override;
  bool setGpsConfig(bool enabled, uint32_t baud_rate, uint32_t interval_secs,
                    bool share_loc_in_advert) override;
  bool exportIdentityHex(char* out_hex, size_t max_len) override;
  bool exportIdentityToSd() override;
  bool importIdentityHex(const char* hex_str) override;
  bool factoryReset() override;
  void rebootToDfu() override;

  bool runCliCommand(const char* cmd, char* out_reply, size_t max_len) override;
  bool sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) override;
  bool sendRemoteLogout(const uint8_t target_pubkey[32]) override;
  bool requestRemoteStatus(const uint8_t target_pubkey[32]) override;
  bool sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) override;
  bool shareContactZeroHop(const uint8_t target_pubkey[32]) override;
  bool findPath(const uint8_t target_pubkey[32]) override;
  bool syncDeviceTime(uint32_t unix_ts) override;
  bool setTuningParams(float rx_delay, uint8_t interference_db, uint8_t agc_sec, bool double_ack) override;
  bool setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) override;
  bool setAutoAddConfig(bool enabled, uint8_t max_hops) override;
  bool setPathHashMode(uint8_t hash_bytes) override;
  bool rebootRadio() override;
  bool exportSettingsToSd(bool overwrite = true) override;
  bool importSettingsFromSd() override;
  bool requestRemoteBinary(const uint8_t target_pubkey[32], const char* subcmd) override;
  bool scanNearbyNodes() override;
  bool useLastAdvertPath(const uint8_t target_pubkey[32]) override;
  bool exportContactUri(const uint8_t target_pubkey[32], char* out_uri, size_t max_len) override;
  bool importContactUri(const char* uri_or_hex) override;
  bool getCustomVars(char* out_vars, size_t max_len) override;
  bool setCustomVar(const char* key_val) override;
  bool setRegion(core::RadioRegionId id) override;
  bool exportContactsJson(char* out_path, size_t max_len) override;
  bool exportGpsTrackGpx(char* out_path, size_t max_len) override;

private:
  void enforceRadioPolicy();
  void applyRadioConfig(const core::RadioConfig& applied);
  core::RadioConfig radioFromPrefs() const;
  void notePending(core::RemoteRequestKind kind, const uint8_t pub_key[32], uint32_t tag, uint32_t est_timeout);
  bool sendTrackedRequest(core::RemoteRequestKind kind, const uint8_t pub_key[32], const uint8_t* req,
                          uint8_t req_len, bool force_flood);

  struct PendingRequest {
    core::RemoteRequestKind kind = core::RemoteRequestKind::None;
    uint8_t  prefix[8] = {0};
    uint32_t tag = 0;
    uint32_t expires_ms = 0;
    bool     used = false;
  };
  static constexpr size_t kMaxPending = 8;
  PendingRequest _pending[kMaxPending] = {};
  uint32_t _discover_tag = 0;
  uint32_t _discover_until_ms = 0;

  core::AppStore& _store;
  core::MeshEventBus& _bus;
  mesh::MainBoard* _board = nullptr;
  MultiSerialInterface* _ifaces = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs* _prefs = nullptr;
  hal::SdCard* _sd = nullptr;
  uint32_t _last_poll_ms = 0;
  uint32_t _reboot_at_ms = 0;  // pending restart after an identity import
  static constexpr size_t kDutySlots = 61;  // 60 one-minute intervals
  uint32_t _duty_ms[kDutySlots] = {};
  unsigned long _duty_air[kDutySlots] = {};
  size_t _duty_head = 0;
  size_t _duty_count = 0;
};

}  // namespace faro::platform
#endif
