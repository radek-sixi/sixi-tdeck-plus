// SPDX-License-Identifier: Apache-2.0
// sim/VirtualMeshActions.h — 6-node virtual Alpine mesh harness for simulator & integration tests
#pragma once

#include "core/MeshActions.h"
#include "core/AppStore.h"
#include "core/EventBus.h"

namespace faro::sim {

class VirtualMeshActions : public core::IMeshActions {
public:
  VirtualMeshActions(core::AppStore& store, core::MeshEventBus& bus);

  // Populates realistic 6-node Alpine mesh topology, messages, channels and trace
  void seedDemoTopology();

  // Simulates periodic radio traffic / repeater adverts
  void tick(uint32_t now_ms);

  // IMeshActions implementation
  bool resendDirectMessage(uint32_t msg_id) override;
  bool sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                         uint32_t& out_ack_crc, core::DeliveryState& out_state) override;
  bool sendChannelMessage(uint8_t channel_index, const char* text) override;
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
  size_t scanWifiNetworks(char ssids[][33], int16_t rssis[], size_t max_networks) override;
  bool setBlePin(uint32_t pin) override;
  bool setRepeatEnabled(bool enabled) override;
  bool setDefaultScope(const char* scope_name) override;
  bool setGpsConfig(bool enabled, uint32_t baud_rate, uint32_t interval_secs,
                    bool share_loc_in_advert) override;
  bool exportIdentityHex(char* out_hex, size_t max_len) override;
  bool importIdentityHex(const char* hex_str) override;
  bool factoryReset() override;
  void rebootToDfu() override;

  bool runCliCommand(const char* cmd, char* out_reply, size_t max_len) override;
  bool sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) override;
  bool sendRemoteLogout(const uint8_t target_pubkey[32]) override;
  bool requestRemoteStatus(const uint8_t target_pubkey[32]) override;
  bool requestRemoteBinary(const uint8_t target_pubkey[32], const char* subcmd) override;
  bool sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) override;
  bool setTuningParams(float rx_delay, uint8_t interference_db, uint8_t agc_sec, bool double_ack) override;
  bool setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) override;
  bool setAutoAddConfig(bool enabled, uint8_t max_hops) override;
  bool setPathHashMode(uint8_t hash_bytes) override;
  bool useLastAdvertPath(const uint8_t target_pubkey[32]) override;
  bool exportContactUri(const uint8_t target_pubkey[32], char* out_uri, size_t max_len) override;
  bool importContactUri(const char* uri_or_hex) override;
  bool getCustomVars(char* out_vars, size_t max_len) override;
  bool setCustomVar(const char* key_val) override;
  bool exportSettingsToSd(bool overwrite = true) override;
  bool importSettingsFromSd() override;
  bool setRegion(core::RadioRegionId id) override;
  bool exportContactsJson(char* out_path, size_t max_len) override;
  bool exportGpsTrackGpx(char* out_path, size_t max_len) override;
  bool scanNearbyNodes() override;

  uint32_t zeroHopAdvertsSent() const { return _zero_hop_adverts; }
  uint32_t floodAdvertsSent() const { return _flood_adverts; }

private:
  core::AppStore& _store;
  core::MeshEventBus& _bus;
  uint32_t _next_crc = 0x10002000;
  uint32_t _next_tag = 500;
  uint32_t _zero_hop_adverts = 0;
  uint32_t _flood_adverts = 0;
};

}  // namespace faro::sim
