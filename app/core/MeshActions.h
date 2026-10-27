// SPDX-License-Identifier: Apache-2.0
// app/core/MeshActions.h — Pure virtual IMeshActions contract (ADR-006, ADR-011)
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Types.h"

namespace faro::core {

class IMeshActions {
public:
  virtual ~IMeshActions() = default;

  // Messaging
  virtual bool sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                                 uint32_t& out_ack_crc, DeliveryState& out_state) = 0;
  virtual bool sendChannelMessage(uint8_t channel_index, const char* text) = 0;
  // Transmit an already stored outgoing DM again (retry / store-and-forward); updates its record.
  virtual bool resendDirectMessage(uint32_t msg_id) { (void)msg_id; return false; }

  // Discovery & Adverts
  virtual bool sendZeroHopAdvert() = 0;
  virtual bool sendFloodAdvert() = 0;
  virtual void syncStore() = 0;

  // Contacts & Paths
  virtual bool addOrUpdateContact(const ContactRecord& contact) = 0;
  virtual bool removeContact(const uint8_t pub_key[32]) = 0;
  virtual bool resetContactPath(const uint8_t pub_key[32]) = 0;
  virtual bool setPreferredRepeater(const uint8_t repeater_pubkey[32]) = 0;

  // Channels
  virtual bool addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) = 0;
  virtual bool addHashtagChannel(const char* hashtag_name) = 0;
  virtual bool removeChannel(uint8_t channel_index) = 0;

  // Diagnostics, Trace & Telemetry
  virtual bool sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) = 0;
  virtual bool requestTelemetry(const uint8_t target_pubkey[32]) = 0;

  // Radio & Node Settings
  virtual bool setRadioConfig(const RadioConfig& cfg) = 0;
  virtual bool setNodeName(const char* name) = 0;
  virtual bool setWifiConfig(bool enabled, const char* ssid, const char* password) = 0;
  virtual bool setBlePin(uint32_t pin) = 0;
  virtual bool setRepeatEnabled(bool enabled) = 0;
  virtual bool setDefaultScope(const char* scope_name) = 0;
  virtual bool setGpsConfig(bool enabled, uint32_t baud_rate, uint32_t interval_secs,
                            bool share_loc_in_advert) = 0;


  // Identity & Maintenance
  virtual bool exportIdentityHex(char* out_hex, size_t max_len) = 0;
  virtual bool importIdentityHex(const char* hex_str) = 0;
  // Write the identity to the SD card (/faro/identity.bak); UI must confirm first.
  virtual bool exportIdentityToSd() { return false; }
  virtual bool factoryReset() = 0;
  virtual void rebootToDfu() = 0;

  // Extended actions. The defaults report "not done" so a backend without them never fakes success.
  virtual bool runCliCommand(const char* cmd, char* out_reply, size_t max_len) {
    (void)cmd;
    if (out_reply && max_len > 0) out_reply[0] = '\0';
    return false;
  }
  virtual bool sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) {
    (void)target_pubkey; (void)password; return false;
  }
  virtual bool sendRemoteLogout(const uint8_t target_pubkey[32]) {
    (void)target_pubkey; return false;
  }
  virtual bool requestRemoteStatus(const uint8_t target_pubkey[32]) {
    (void)target_pubkey; return false;
  }
  // subcmd: "neighbors", "acl" or "mma"
  virtual bool requestRemoteBinary(const uint8_t target_pubkey[32], const char* subcmd) {
    (void)target_pubkey; (void)subcmd; return false;
  }
  virtual bool sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) {
    (void)target_pubkey; (void)cli_cmd; return false;
  }
  virtual bool shareContactZeroHop(const uint8_t target_pubkey[32]) {
    (void)target_pubkey; return false;
  }
  virtual bool findPath(const uint8_t target_pubkey[32]) {
    (void)target_pubkey; return false;
  }
  // Zero-hop discover request; answers arrive as NodeDiscovered events
  virtual bool scanNearbyNodes() { return false; }
  virtual bool syncDeviceTime(uint32_t unix_ts) {
    (void)unix_ts; return false;
  }
  virtual bool setTuningParams(float rx_delay, uint8_t interference_db, uint8_t agc_sec, bool double_ack) {
    (void)rx_delay; (void)interference_db; (void)agc_sec; (void)double_ack; return false;
  }
  virtual bool setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) {
    (void)telemetry_perm; (void)loc_policy; return false;
  }
  virtual bool setAutoAddConfig(bool enabled, uint8_t max_hops) {
    (void)enabled; (void)max_hops; return false;
  }
  virtual bool setPathHashMode(uint8_t hash_bytes) {
    (void)hash_bytes; return false;
  }
  virtual bool rebootRadio() { return false; }
  virtual bool useLastAdvertPath(const uint8_t target_pubkey[32]) {
    (void)target_pubkey; return false;
  }
  virtual bool exportContactUri(const uint8_t target_pubkey[32], char* out_uri, size_t max_len) {
    (void)target_pubkey;
    if (out_uri && max_len > 0) out_uri[0] = '\0';
    return false;
  }
  virtual bool importContactUri(const char* uri_or_hex) {
    (void)uri_or_hex; return false;
  }
  virtual bool getCustomVars(char* out_vars, size_t max_len) {
    if (out_vars && max_len > 0) out_vars[0] = '\0';
    return false;
  }
  virtual bool setCustomVar(const char* key_val) {
    (void)key_val; return false;
  }
  virtual bool reconnectTarget(const char* target_spec) {
    (void)target_spec; return false;
  }
  virtual bool scanLanRadios(char* out_found, size_t max_len) {
    if (out_found && max_len > 0) out_found[0] = '\0';
    return false;
  }
  virtual bool exportSettingsToSd(bool overwrite = true) {
    (void)overwrite;
    return false;
  }
  virtual bool importSettingsFromSd() {
    return false;
  }

  // Radio region: applies the region's first preset, power ceiling and duty cycle (Sixi: EU 868 only)
  virtual bool setRegion(RadioRegionId id) { (void)id; return false; }

  // Exports (device: SD card /faro/, desktop: /tmp); out_path gets the written file or the error
  virtual bool exportContactsJson(char* out_path, size_t max_len) {
    if (out_path && max_len > 0) out_path[0] = '\0';
    return false;
  }
  virtual bool exportGpsTrackGpx(char* out_path, size_t max_len) {
    if (out_path && max_len > 0) out_path[0] = '\0';
    return false;
  }
};

}  // namespace faro::core
