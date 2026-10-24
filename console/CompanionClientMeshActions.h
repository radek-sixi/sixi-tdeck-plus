// SPDX-License-Identifier: Apache-2.0
// console/CompanionClientMeshActions.h — Host companion protocol client (USB Serial & TCP 5000)
#pragma once

#ifndef ARDUINO

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "core/AppStore.h"
#include "core/EventBus.h"
#include "core/MeshActions.h"
#include "core/Types.h"

namespace faro::platform::remote {

// Upstream MeshCore companion protocol opcodes (external/MeshCore/examples/companion_radio/MyMesh.cpp)
enum CompanionCmd : uint8_t {
  CMD_APP_START              = 1,
  CMD_SEND_TXT_MSG           = 2,
  CMD_SEND_CHANNEL_TXT_MSG   = 3,
  CMD_GET_CONTACTS           = 4,
  CMD_GET_DEVICE_TIME        = 5,
  CMD_SET_DEVICE_TIME        = 6,
  CMD_SEND_SELF_ADVERT       = 7,
  CMD_SET_ADVERT_NAME        = 8,
  CMD_ADD_UPDATE_CONTACT     = 9,
  CMD_SYNC_NEXT_MESSAGE      = 10,
  CMD_SET_RADIO_PARAMS       = 11,
  CMD_SET_RADIO_TX_POWER     = 12,
  CMD_RESET_PATH             = 13,
  CMD_SET_ADVERT_LATLON      = 14,
  CMD_REMOVE_CONTACT         = 15,
  CMD_SHARE_CONTACT          = 16,
  CMD_EXPORT_CONTACT         = 17,
  CMD_IMPORT_CONTACT         = 18,
  CMD_REBOOT                 = 19,
  CMD_GET_BATT_AND_STORAGE   = 20,
  CMD_SET_TUNING_PARAMS      = 21,
  CMD_DEVICE_QUERY           = 22,
  CMD_SEND_LOGIN             = 26,
  CMD_SEND_STATUS_REQ        = 27,
  CMD_LOGOUT                 = 29,
  CMD_GET_CHANNEL            = 31,
  CMD_SET_CHANNEL            = 32,
  CMD_SEND_TRACE_PATH        = 36,
  CMD_SET_DEVICE_PIN         = 37,
  CMD_SET_OTHER_PARAMS       = 38,
  CMD_SEND_TELEMETRY_REQ     = 39,
  CMD_GET_CUSTOM_VARS        = 40,
  CMD_SET_CUSTOM_VAR         = 41,
  CMD_GET_ADVERT_PATH        = 42,
  CMD_SEND_BINARY_REQ        = 50,
  CMD_FACTORY_RESET          = 51,
  CMD_SEND_PATH_DISCOVERY_REQ = 52,
  CMD_SET_FLOOD_SCOPE_KEY    = 54,
  CMD_SEND_CONTROL_DATA      = 55,
  CMD_GET_STATS              = 56,
  CMD_SET_AUTOADD_CONFIG     = 58,
  CMD_GET_AUTOADD_CONFIG     = 59,
  CMD_GET_ALLOWED_REPEAT_FREQ = 60,
  CMD_SET_PATH_HASH_MODE     = 61,
  CMD_SEND_CHANNEL_DATA      = 62,
  CMD_SET_DEFAULT_FLOOD_SCOPE = 63,
  CMD_GET_DEFAULT_FLOOD_SCOPE = 64,
  CMD_RUN_CLI_COMMAND        = 66,
};

enum CompanionResp : uint8_t {
  RESP_CODE_OK                  = 0,
  RESP_CODE_ERR                 = 1,
  RESP_CODE_CONTACTS_START      = 2,
  RESP_CODE_CONTACT             = 3,
  RESP_CODE_END_OF_CONTACTS     = 4,
  RESP_CODE_SELF_INFO           = 5,
  RESP_CODE_SENT                = 6,
  RESP_CODE_CONTACT_MSG_RECV    = 7,
  RESP_CODE_CHANNEL_MSG_RECV    = 8,
  RESP_CODE_CURR_TIME           = 9,
  RESP_CODE_NO_MORE_MESSAGES    = 10,
  RESP_CODE_EXPORT_CONTACT      = 11,
  RESP_CODE_BATT_AND_STORAGE    = 12,
  RESP_CODE_DEVICE_INFO         = 13,
  RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
  RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
  RESP_CODE_CHANNEL_INFO        = 18,
  RESP_CODE_CUSTOM_VARS         = 21,
  RESP_CODE_ADVERT_PATH         = 22,
  RESP_CODE_STATS               = 24,
  RESP_CODE_CLI_REPLY           = 29,
};

enum CompanionPush : uint8_t {
  PUSH_CODE_ADVERT         = 0x80,
  PUSH_CODE_PATH_UPDATED   = 0x81,
  PUSH_CODE_SEND_CONFIRMED = 0x82,
  PUSH_CODE_MSG_WAITING    = 0x83,
  PUSH_CODE_RAW_DATA       = 0x84,
  PUSH_CODE_LOGIN_SUCCESS  = 0x85,
  PUSH_CODE_STATUS_RESPONSE = 0x87,
  PUSH_CODE_LOG_RX_DATA    = 0x88,
  PUSH_CODE_TRACE_DATA     = 0x89,
  PUSH_CODE_NEW_ADVERT     = 0x8A,
  PUSH_CODE_TELEMETRY_RESPONSE = 0x8B,
  PUSH_CODE_BINARY_RESPONSE = 0x8C,
  PUSH_CODE_CHANNEL_DATA   = 0x8D,
};

class CompanionClientMeshActions final : public faro::core::IMeshActions {
public:
  CompanionClientMeshActions(faro::core::AppStore& store, faro::core::MeshEventBus& bus);
  ~CompanionClientMeshActions() override;

  // Connect over TCP (e.g. 192.168.4.1:5000). token16 is used by the Sixi edition only.
  bool connectTcp(const char* host, uint16_t port, const uint8_t* token16 = nullptr);

  // Connect over USB CDC serial port (e.g. /dev/ttyACM0 @ 115200)
  bool connectSerial(const char* dev_path, int baud = 115200);

  void disconnect();
  bool isConnected() const { return _fd >= 0; }

  // Polls non-blocking for incoming '>' frames (PUSH_CODE_* and responses)
  void poll();

  // IMeshActions implementation
  bool resendDirectMessage(uint32_t msg_id) override;
  bool sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                         uint32_t& out_ack_crc, faro::core::DeliveryState& out_state) override;
  bool sendChannelMessage(uint8_t channel_index, const char* text) override;

  bool sendZeroHopAdvert() override;
  bool sendFloodAdvert() override;
  void syncStore() override;

  bool addOrUpdateContact(const faro::core::ContactRecord& contact) override;
  bool removeContact(const uint8_t pub_key[32]) override;
  bool resetContactPath(const uint8_t pub_key[32]) override;
  bool setPreferredRepeater(const uint8_t repeater_pubkey[32]) override;

  bool addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) override;
  bool addHashtagChannel(const char* hashtag_name) override;
  bool removeChannel(uint8_t channel_index) override;

  bool sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) override;
  bool requestTelemetry(const uint8_t target_pubkey[32]) override;

  bool setRadioConfig(const faro::core::RadioConfig& cfg) override;
  bool setNodeName(const char* name) override;
  bool setWifiConfig(bool enabled, const char* ssid, const char* password) override;
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
  bool shareContactZeroHop(const uint8_t target_pubkey[32]) override;
  bool findPath(const uint8_t target_pubkey[32]) override;
  bool scanNearbyNodes() override;
  bool syncDeviceTime(uint32_t unix_ts) override;
  bool setTuningParams(float rx_delay, uint8_t interference_db, uint8_t agc_sec, bool double_ack) override;
  bool setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) override;
  bool setAutoAddConfig(bool enabled, uint8_t max_hops) override;
  bool setPathHashMode(uint8_t hash_bytes) override;
  bool rebootRadio() override;
  bool useLastAdvertPath(const uint8_t target_pubkey[32]) override;
  bool exportContactUri(const uint8_t target_pubkey[32], char* out_uri, size_t max_len) override;
  bool importContactUri(const char* uri_or_hex) override;
  bool getCustomVars(char* out_vars, size_t max_len) override;
  bool setCustomVar(const char* key_val) override;
  bool reconnectTarget(const char* target_spec) override;
  bool scanLanRadios(char* out_found, size_t max_len) override;

private:
  bool sendFrame(const uint8_t* payload, size_t len);
  bool recvFrameBlocking(std::vector<uint8_t>& out, int timeout_ms);
  void handleIncomingFrame(const uint8_t* data, size_t len);

  faro::core::AppStore& _store;
  faro::core::MeshEventBus& _bus;
  int _fd = -1;
  std::vector<uint8_t> _rx_buf;
  uint32_t _last_sent_crc = 0x1000;
  uint32_t _trace_seq = 1;
  char _last_host[64] = {0};
  uint16_t _last_port = 5000;
  uint32_t _last_reconnect_ms = 0;
};

}  // namespace faro::platform::remote

#endif  // !ARDUINO
