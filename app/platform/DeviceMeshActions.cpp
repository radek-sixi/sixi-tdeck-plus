// SPDX-License-Identifier: Apache-2.0
// app/platform/DeviceMeshActions.cpp — Real ESP32-S3 MeshCore binding
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <SHA256.h>

extern unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);

#include "DeviceMeshActions.h"
#include "core/Random.h"

extern DataStore store;

#ifdef ENABLE_WIFI_INTERFACE
#include <helpers/wifi/SerialWifiInterface.h>
extern SerialWifiInterface wifi_interface;
#endif

namespace faro::platform {

using namespace faro::core;

namespace {

bool isAllZero(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (buf[i] != 0) return false;
  }
  return true;
}
}  // namespace

DeviceMeshActions::DeviceMeshActions(AppStore& app_store, MeshEventBus& bus)
  : _store(app_store), _bus(bus) {}

void DeviceMeshActions::begin(mesh::MainBoard* board, MultiSerialInterface* ifaces,
                              SensorManager* sensors, NodePrefs* prefs, hal::SdCard* sd) {
  _board = board;
  _ifaces = ifaces;
  _sensors = sensors;
  _prefs = prefs;
  _sd = sd;


  syncStore();
  enforceRadioPolicy();

  const RadioConfig& r = _store.radio();
  Serial.printf("[FARO] radio %.3f MHz BW%.1f SF%u CR%u %d dBm (ERP %.1f dBm) duty<=%.0f%% | BLE PIN %s | "
                "WiFi %s | contacts %u\n",
                double(r.freq_mhz), double(r.bw_khz), unsigned(r.sf), unsigned(r.cr), int(r.tx_power_dbm),
                double(RadioBandPolicy::erpDbm(r.tx_power_dbm)),
                double(RadioBandPolicy::dutyCyclePercent(r.airtime_factor)),
                (_prefs && _prefs->ble_pin == 0) ? "random" : "fixed",
                _store.status().wifi_enabled ? "on" : "off", unsigned(_store.contactCount()));
}

// Bring stored radio settings (possibly written by another firmware or the phone app) inside the
// band policy of Types.h. Every change is logged and shown in Settings > Radio; nothing silent.
void DeviceMeshActions::enforceRadioPolicy() {
  if (!_prefs) return;
  RadioConfig cur = _store.radio();
  RadioConfig fixed = RadioConfig::sanitized(cur);
  if (!RadioBandPolicy::channelInBand(fixed.freq_mhz, fixed.bw_khz) ||
      !RadioConfig::isValidSx1262Bandwidth(fixed.bw_khz)) {
    const RadioPreset& def = kRadioPresets[0];
    fixed.freq_mhz = def.freq_mhz;
    fixed.bw_khz = def.bw_khz;
    fixed.sf = def.sf;
    fixed.cr = def.cr;
    fixed.client_repeat = false;
  }
  const bool changed = fabsf(fixed.freq_mhz - cur.freq_mhz) > 0.0005f || fabsf(fixed.bw_khz - cur.bw_khz) > 0.01f ||
                       fixed.sf != cur.sf || fixed.cr != cur.cr || fixed.tx_power_dbm != cur.tx_power_dbm ||
                       fabsf(fixed.airtime_factor - cur.airtime_factor) > 0.001f ||
                       fixed.client_repeat != cur.client_repeat;
  if (!changed) return;
  snprintf(_store.status().radio_notice, sizeof(_store.status().radio_notice),
           "Radio limited to band rules: %.3f MHz BW%.1f %ddBm duty<=%.0f%%",
           double(fixed.freq_mhz), double(fixed.bw_khz), int(fixed.tx_power_dbm),
           double(RadioBandPolicy::dutyCyclePercent(fixed.airtime_factor)));
  Serial.printf("[FARO] radio policy: %.3f/%.1f/SF%u/%ddBm/af%.1f -> %.3f/%.1f/SF%u/%ddBm/af%.1f\n",
                double(cur.freq_mhz), double(cur.bw_khz), unsigned(cur.sf), int(cur.tx_power_dbm),
                double(cur.airtime_factor), double(fixed.freq_mhz), double(fixed.bw_khz),
                unsigned(fixed.sf), int(fixed.tx_power_dbm), double(fixed.airtime_factor));
  applyRadioConfig(fixed);
}

void DeviceMeshActions::syncStore() {
  if (_prefs) {
    SystemStatus& st = _store.status();
    strncpy(st.node_name, _prefs->node_name[0] ? _prefs->node_name : "Faro T-Deck",
            sizeof(st.node_name) - 1);
    memcpy(st.self_pubkey, the_mesh.self_id.pub_key, 32);
    st.ble_pin = the_mesh.getBLEPin();
#ifdef ENABLE_WIFI_INTERFACE
    st.wifi_enabled = (_prefs->wifi_enabled != 0);
    strncpy(st.wifi_ssid, _prefs->getWifiSSID(), sizeof(st.wifi_ssid) - 1);
    strncpy(st.wifi_pwd, _prefs->wifi_pwd, sizeof(st.wifi_pwd) - 1);
#endif
    _store.gps().enabled = (_prefs->gps_enabled != 0);
    strncpy(st.active_scope_name, _prefs->default_scope_name[0] ? _prefs->default_scope_name : "*",
            sizeof(st.active_scope_name) - 1);

    // Sync full MeshCore Android App settings from NodePrefs
    st.telemetry_perm = _prefs->telemetry_mode_base % 3;
    st.telemetry_loc_perm = _prefs->telemetry_mode_loc % 3;
    st.telemetry_env_perm = _prefs->telemetry_mode_env % 3;
    st.loc_policy = (_prefs->advert_loc_policy == ADVERT_LOC_SHARE) ? 2 : 0;
    st.auto_add_contacts = (_prefs->manual_add_contacts == 0);
    st.auto_add_max_hops = _prefs->autoadd_max_hops;
    st.path_hash_bytes = (_prefs->path_hash_mode <= 2) ? uint8_t(_prefs->path_hash_mode + 1) : 1;
    st.multi_acks = _prefs->multi_acks;
    st.double_acks = (_prefs->multi_acks > 0);
    st.cad_enabled = (_prefs->cad_enabled != 0);
    st.rx_delay = _prefs->rx_delay_base;
    st.tx_delay_factor = _prefs->tx_delay_factor;
    st.direct_tx_delay_factor = _prefs->direct_tx_delay_factor;
    st.interference_db = _prefs->interference_threshold > 0 ? _prefs->interference_threshold : 14;
    st.agc_reset_sec = _prefs->agc_reset_interval > 0 ? uint8_t(_prefs->agc_reset_interval * 4) : 20;
    st.tz_offset_hours = _prefs->tz_offset;
    if (_prefs->node_lat != 0.0 || _prefs->node_lon != 0.0) {
      st.manual_lat_deg = _prefs->node_lat;
      st.manual_lon_deg = _prefs->node_lon;
    }
    _store.uiPrefs().quiet_mode = (_prefs->buzzer_quiet != 0);

    RadioConfig& r = _store.radio();
    r.freq_mhz = _prefs->freq > 0 ? _prefs->freq : 869.618f;
    r.bw_khz = _prefs->bw > 0 ? _prefs->bw : 62.5f;
    r.sf = _prefs->sf > 0 ? _prefs->sf : 8;
    r.cr = _prefs->cr > 0 ? _prefs->cr : 8;
    r.tx_power_dbm = _prefs->tx_power_dbm != 0 ? _prefs->tx_power_dbm : LORA_TX_POWER;
    r.rx_boosted_gain = (_prefs->rx_boosted_gain != 0);
    r.client_repeat = _prefs->isRepeatEn();
    r.path_hash_mode = _prefs->path_hash_mode;
    r.airtime_factor = _prefs->airtime_factor;
  }

  // Sync contacts from BaseChatMesh
  int n_contacts = the_mesh.getNumContacts();
  for (int i = 0; i < n_contacts; ++i) {
    ContactInfo ci{};
    if (!the_mesh.getContactByIdx(uint32_t(i), ci)) continue;
    ContactRecord c{};
    memcpy(c.pub_key, ci.id.pub_key, 32);
    strncpy(c.name, ci.name, sizeof(c.name) - 1);
    c.role = static_cast<NodeRole>(ci.type);
    c.flags = ci.flags;
    c.out_path_len = ci.out_path_len;
    if (ci.out_path_len < 64) {
      memcpy(c.out_path, ci.out_path, ci.out_path_len);
    }
    c.last_advert_ts = ci.last_advert_timestamp;
    c.lastmod_ts = ci.lastmod;
    c.gps_lat_e6 = ci.gps_lat;
    c.gps_lon_e6 = ci.gps_lon;
    c.valid = true;
    _store.upsertContact(c);
  }

  // Ensure Channel 0 ("Public") is always valid in BaseChatMesh so searchChannelsByHash() never skips it
  {
    ChannelDetails cd0{};
    if (!the_mesh.getChannel(0, cd0) || cd0.name[0] == '\0') {
      static const uint8_t kPublicPsk[16] = {
        0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
        0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
      };
      memset(&cd0, 0, sizeof(cd0));
      strncpy(cd0.name, "Public", sizeof(cd0.name) - 1);
      memcpy(cd0.channel.secret, kPublicPsk, sizeof(kPublicPsk));
      mesh::Utils::sha256(cd0.channel.hash, sizeof(cd0.channel.hash), cd0.channel.secret, 16);
      the_mesh.setChannel(0, cd0);
      ::store.saveChannels(&the_mesh);
    }
  }

  // Ensure #ch (Switzerland regional channel) is present in slot 1 if no custom channels exist yet
  {
    bool has_custom_channel = false;
    for (int idx = 1; idx < MAX_GROUP_CHANNELS; ++idx) {
      ChannelDetails cd{};
      if (the_mesh.getChannel(idx, cd) && cd.name[0] != '\0') {
        has_custom_channel = true;
        break;
      }
    }
    if (!has_custom_channel) {
      ChannelDetails ch_cd{};
      memset(&ch_cd, 0, sizeof(ch_cd));
      strncpy(ch_cd.name, "#ch", sizeof(ch_cd.name) - 1);
      uint8_t digest[32] = {0};
      SHA256 sha;
      sha.update("#ch", 3);
      sha.finalize(digest, sizeof(digest));
      memcpy(ch_cd.channel.secret, digest, 16);
      mesh::Utils::sha256(ch_cd.channel.hash, sizeof(ch_cd.channel.hash), ch_cd.channel.secret, 16);
      the_mesh.setChannel(1, ch_cd);
      ::store.saveChannels(&the_mesh);
    }
  }

  // Sync channels from BaseChatMesh
  for (int idx = 0; idx < MAX_GROUP_CHANNELS; ++idx) {
    ChannelDetails cd{};
    if (!the_mesh.getChannel(idx, cd)) continue;
    if (!cd.name[0] && idx > 0) continue;
    ChannelRecord ch{};
    ch.index = uint8_t(idx);
    strncpy(ch.name, cd.name[0] ? cd.name : "Public", sizeof(ch.name) - 1);
    memcpy(ch.secret, cd.channel.secret, 32);
    bool is_256 = !isAllZero(&cd.channel.secret[16], 16);
    ch.secret_len = is_256 ? 32 : 16;
    ch.is_hashtag = (cd.name[0] == '#');
    ch.valid = true;
    _store.upsertChannel(ch);
  }
}

void DeviceMeshActions::pollTelemetry(uint32_t now_ms, uint16_t batt_mv, uint8_t batt_pct,
                                      bool charging, bool sd_mounted,
                                      uint32_t sd_used_mb, uint32_t sd_total_mb,
                                      uint32_t nmea_sentences) {
  if (_reboot_at_ms != 0 && int32_t(now_ms - _reboot_at_ms) >= 0 && _board) {
    _board->reboot();
  }
  if (now_ms - _last_poll_ms < 1000) return;
  _last_poll_ms = now_ms;

  SystemStatus& st = _store.status();
  st.batt_mv = batt_mv;
  st.batt_pct = batt_pct;
  st.charging = charging;
  st.low_batt_warn = (!charging && batt_pct < 10);
  st.sd_mounted = sd_mounted;
  st.sd_used_mb = sd_used_mb;
  st.sd_total_mb = sd_total_mb;
  st.uptime_secs = now_ms / 1000U;
  st.free_heap = ESP.getFreeHeap();
  st.free_psram = ESP.getFreePsram();
#ifdef ENABLE_WIFI_INTERFACE
  st.wifi_connected = (_prefs && _prefs->wifi_enabled && _prefs->wifi_ssid[0] && WiFi.status() == WL_CONNECTED);
  if (st.wifi_connected) {
    strncpy(st.wifi_ip, WiFi.localIP().toString().c_str(), sizeof(st.wifi_ip) - 1);
  }
#endif
  if (_ifaces) {
    st.ble_connected = _ifaces->isConnected();
  }
  // Measured TX airtime over the last hour (MeshCore's own counter), one sample per minute
  if (_duty_count == 0 || now_ms - _duty_ms[(_duty_head + kDutySlots - 1) % kDutySlots] >= 60000U) {
    _duty_ms[_duty_head] = now_ms;
    _duty_air[_duty_head] = the_mesh.getTotalAirTime();
    _duty_head = (_duty_head + 1) % kDutySlots;
    if (_duty_count < kDutySlots) ++_duty_count;
  }
  const size_t oldest = (_duty_head + kDutySlots - _duty_count) % kDutySlots;
  const uint32_t span_ms = now_ms - _duty_ms[oldest];
  const unsigned long air_ms = the_mesh.getTotalAirTime() - _duty_air[oldest];
  st.duty_cycle_permille = span_ms > 0 ? uint16_t((uint64_t(air_ms) * 1000U) / span_ms) : 0;
  st.duty_cycle_throttled = false;  // MeshCore enforces the limit itself through the airtime factor

  if (_sensors) {
    GpsFix& g = _store.gps();
    g.lat_deg = _sensors->node_lat;
    g.lon_deg = _sensors->node_lon;
    g.alt_m = _sensors->node_altitude;
    g.valid = (g.lat_deg != 0.0 || g.lon_deg != 0.0);
    g.nmea_sentences = nmea_sentences;
  }
}

bool DeviceMeshActions::resendDirectMessage(uint32_t msg_id) {
  MessageRecord* m = _store.findMessageByIdMutable(msg_id);
  if (!m || !m->outgoing || m->is_channel) return false;
  const ContactRecord* c = _store.findContactByPrefix(m->peer_prefix);
  ContactInfo* ci = c ? the_mesh.lookupContactByPubKey(c->pub_key, PUB_KEY_SIZE) : nullptr;
  if (!ci) {
    m->delivery_state = DeliveryState::Failed;
    _store.bumpVersion();
    return false;
  }
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  uint32_t ack = 0, est_timeout = 0;
  int res = the_mesh.sendMessage(*ci, ts, m->retry_count, m->text, ack, est_timeout);
  if (res == MSG_SEND_FAILED) {
    m->delivery_state = DeliveryState::Failed;
  } else {
    m->ack_crc = ack;
    m->sent_ms = millis();
    m->delivery_state = (res == MSG_SEND_SENT_DIRECT) ? DeliveryState::SentDirect : DeliveryState::SentFlood;
  }
  _store.bumpVersion();
  return res != MSG_SEND_FAILED;
}

bool DeviceMeshActions::sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                                          uint32_t& out_ack_crc, DeliveryState& out_state) {
  if (!peer_pubkey || !text || !text[0]) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(peer_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;

  uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  uint32_t est_timeout = 0;
  int res = the_mesh.sendMessage(*ci, ts, 0, text, out_ack_crc, est_timeout);
  if (res == MSG_SEND_FAILED) {
    out_state = DeliveryState::Failed;
    return false;
  }
  out_state = (res == MSG_SEND_SENT_DIRECT) ? DeliveryState::SentDirect : DeliveryState::SentFlood;

  MessageRecord m{};
  m.is_channel = false;
  memcpy(m.peer_prefix, peer_pubkey, 6);
  strncpy(m.sender_name, _store.status().node_name, sizeof(m.sender_name) - 1);
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.timestamp = ts;
  m.sent_ms = millis();
  m.ack_crc = out_ack_crc;
  m.delivery_state = out_state;
  m.outgoing = true;
  _store.appendMessage(m);
  _store.status().tx_packets++;
  if (out_state == DeliveryState::SentDirect) _store.status().direct_tx++;
  else _store.status().flood_tx++;
  return true;
}

bool DeviceMeshActions::sendChannelMessage(uint8_t channel_index, const char* text) {
  if (!text || !text[0]) return false;
  ChannelDetails cd{};
  if (!the_mesh.getChannel(channel_index, cd)) return false;

  uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  if (!the_mesh.sendGroupMessage(ts, cd.channel, _store.status().node_name, text, strlen(text))) {
    return false;
  }

  MessageRecord m{};
  m.is_channel = true;
  m.channel_index = channel_index;
  strncpy(m.sender_name, _store.status().node_name, sizeof(m.sender_name) - 1);
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.timestamp = ts;
  m.sent_ms = millis();
  m.delivery_state = DeliveryState::SentFlood;
  m.outgoing = true;
  _store.appendMessage(m);
  _store.status().tx_packets++;
  _store.status().flood_tx++;
  return true;
}

bool DeviceMeshActions::sendZeroHopAdvert() {
  if (!_prefs) return false;
  mesh::Packet* pkt = (_prefs->advert_loc_policy == ADVERT_LOC_NONE)
                        ? the_mesh.createSelfAdvert(_prefs->node_name)
                        : the_mesh.createSelfAdvert(_prefs->node_name,
                                                    _sensors ? _sensors->node_lat : 0.0,
                                                    _sensors ? _sensors->node_lon : 0.0);
  if (!pkt) return false;
  the_mesh.sendZeroHop(pkt);
  _store.status().tx_packets++;
  _store.status().direct_tx++;
  return true;
}

bool DeviceMeshActions::sendFloodAdvert() {
  if (!the_mesh.advert()) return false;
  _store.status().tx_packets++;
  _store.status().flood_tx++;
  return true;
}

bool DeviceMeshActions::addOrUpdateContact(const ContactRecord& contact) {
  ContactInfo ci{};
  memcpy(ci.id.pub_key, contact.pub_key, 32);
  strncpy(ci.name, contact.name, sizeof(ci.name) - 1);
  ci.type = uint8_t(contact.role);
  ci.flags = contact.flags;
  ci.out_path_len = contact.out_path_len;
  if (contact.out_path_len < 64) {
    memcpy(ci.out_path, contact.out_path, contact.out_path_len);
  }
  ci.gps_lat = contact.gps_lat_e6;
  ci.gps_lon = contact.gps_lon_e6;
  the_mesh.addContact(ci);
  ::store.saveContacts(&the_mesh);
  return _store.upsertContact(contact) != nullptr;
}

bool DeviceMeshActions::removeContact(const uint8_t pub_key[32]) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (ci) {
    the_mesh.removeContact(*ci);
    ::store.saveContacts(&the_mesh);
  }
  return _store.removeContact(pub_key);
}

bool DeviceMeshActions::resetContactPath(const uint8_t pub_key[32]) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (ci) {
    the_mesh.resetPathTo(*ci);
    ::store.saveContacts(&the_mesh);
  }
  if (ContactRecord* cr = _store.findContactByPubKeyMutable(pub_key)) {
    cr->out_path_len = 0xFF;
    _store.bumpVersion();
    return true;
  }
  return false;
}

bool DeviceMeshActions::setPreferredRepeater(const uint8_t repeater_pubkey[32]) {
  if (!repeater_pubkey) return false;
  const ContactRecord* c = _store.findContactByPubKey(repeater_pubkey);
  const char* name = (c && c->name[0]) ? c->name : "Preferred-Repeater";
  int8_t snr = c ? c->last_snr_q4 : 40;
  int16_t rssi = c ? c->last_rssi : -70;
  int32_t lat = c ? c->gps_lat_e6 : 0;
  int32_t lon = c ? c->gps_lon_e6 : 0;
  uint32_t now = millis();
  _store.repeatRoam().onRepeaterHeard(repeater_pubkey, name, snr, rssi, lat, lon, now);
  uint8_t next_pref[32] = {0};
  _store.repeatRoam().evaluate(now, next_pref);
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) {
  if (!name || !name[0] || !secret) return false;
  if (secret_len != 16 && secret_len != 32) return false;

  ChannelDetails cd{};
  memset(&cd, 0, sizeof(cd));
  strncpy(cd.name, name, sizeof(cd.name) - 1);
  memcpy(cd.channel.secret, secret, secret_len);
  mesh::Utils::sha256(cd.channel.hash, sizeof(cd.channel.hash), cd.channel.secret, secret_len);

  int target_idx = -1;
  if (strcasecmp(name, "Public") == 0) {
    target_idx = 0;
  } else {
    int first_empty = -1;
    for (int idx = 1; idx < MAX_GROUP_CHANNELS; ++idx) {
      ChannelDetails existing{};
      if (the_mesh.getChannel(idx, existing)) {
        if (existing.name[0] != '\0' && strcasecmp(existing.name, name) == 0) {
          target_idx = idx;
          break;
        }
        if (existing.name[0] == '\0' && first_empty < 0) {
          first_empty = idx;
        }
      }
    }
    if (target_idx < 0) {
      target_idx = (first_empty >= 1) ? first_empty : (MAX_GROUP_CHANNELS - 1);
    }
  }

  if (!the_mesh.setChannel(target_idx, cd)) return false;
  ::store.saveChannels(&the_mesh);
  syncStore();
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::addHashtagChannel(const char* hashtag_name) {
  if (!hashtag_name || !hashtag_name[0]) return false;
  while (*hashtag_name == ' ') ++hashtag_name;
  if (!hashtag_name[0]) return false;

  char full_name[32] = {0};
  if (hashtag_name[0] == '#') {
    strncpy(full_name, hashtag_name, sizeof(full_name) - 1);
  } else {
    snprintf(full_name, sizeof(full_name), "#%s", hashtag_name);
  }
  // Trim trailing spaces and normalize hashtag to lowercase (e.g. #CH -> #ch) per MeshCore convention
  size_t len = strlen(full_name);
  while (len > 0 && full_name[len - 1] == ' ') {
    full_name[--len] = '\0';
  }
  for (size_t i = 0; i < len; ++i) {
    if (full_name[i] >= 'A' && full_name[i] <= 'Z') {
      full_name[i] = char(full_name[i] - 'A' + 'a');
    }
  }
  if (len <= 1) return false;

  uint8_t digest[32] = {0};
  SHA256 sha;
  sha.update(full_name, len);
  sha.finalize(digest, sizeof(digest));
  return addChannel(full_name, digest, 16);
}

bool DeviceMeshActions::removeChannel(uint8_t channel_index) {
  if (channel_index == 0 || channel_index >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails empty{};
  memset(&empty, 0, sizeof(empty));
  if (!the_mesh.setChannel(channel_index, empty)) return false;
  ::store.saveChannels(&the_mesh);
  return _store.removeChannel(channel_index);
}

bool DeviceMeshActions::sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) {
  if (!target_pubkey) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint32_t est_timeout = 0;
  return the_mesh.sendRequest(*ci, REQ_TYPE_GET_STATUS, out_tag, est_timeout) != MSG_SEND_FAILED;
}

bool DeviceMeshActions::requestTelemetry(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint32_t tag = 0, est_timeout = 0;
  return the_mesh.sendRequest(*ci, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout) != MSG_SEND_FAILED;
}

bool DeviceMeshActions::setRadioConfig(const RadioConfig& cfg) {
  if (!_prefs) return false;
  RadioConfig applied = RadioConfig::sanitized(cfg);
  if (!RadioConfig::isValid(applied)) return false;
  _store.status().radio_notice[0] = '\0';
  applyRadioConfig(applied);
  return true;
}

void DeviceMeshActions::applyRadioConfig(const RadioConfig& applied) {
  _prefs->freq = applied.freq_mhz;
  _prefs->bw = applied.bw_khz;
  _prefs->sf = applied.sf;
  _prefs->cr = applied.cr;
  _prefs->tx_power_dbm = applied.tx_power_dbm;
  _prefs->rx_boosted_gain = applied.rx_boosted_gain ? 1 : 0;
  _prefs->setRepeatEn(applied.client_repeat);
  _prefs->airtime_factor = applied.airtime_factor;
  the_mesh.savePrefs();
  radio_driver.setParams(_prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  radio_driver.setTxPower(_prefs->tx_power_dbm);
  _store.radio() = applied;
  _store.bumpVersion();
}

bool DeviceMeshActions::setNodeName(const char* name) {
  if (!name || !name[0] || !_prefs) return false;
  strncpy(_prefs->node_name, name, sizeof(_prefs->node_name) - 1);
  _prefs->node_name[sizeof(_prefs->node_name) - 1] = '\0';
  the_mesh.savePrefs();
  strncpy(_store.status().node_name, _prefs->node_name, sizeof(_store.status().node_name) - 1);
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setWifiConfig(bool enabled, const char* ssid, const char* password) {
#ifdef ENABLE_WIFI_INTERFACE
  if (!_prefs) return false;
  _prefs->wifi_enabled = enabled ? 1 : 0;
  if (ssid) {
    strncpy(_prefs->wifi_ssid, ssid, sizeof(_prefs->wifi_ssid) - 1);
    strncpy(_store.status().wifi_ssid, ssid, sizeof(_store.status().wifi_ssid) - 1);
  }
  if (password) {
    strncpy(_prefs->wifi_pwd, password, sizeof(_prefs->wifi_pwd) - 1);
    strncpy(_store.status().wifi_pwd, password, sizeof(_store.status().wifi_pwd) - 1);
  }
  the_mesh.savePrefs();
  _store.status().wifi_enabled = enabled;
#if defined(ESP32)
  if (enabled && _prefs->wifi_ssid[0]) {
    WiFi.disconnect();
    WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_pwd);
  } else if (!enabled) {
    WiFi.disconnect(true);
    _store.status().wifi_connected = false;
  }
#endif
  _store.bumpVersion();
  return true;
#else
  (void)enabled; (void)ssid; (void)password;
  return false;
#endif
}

bool DeviceMeshActions::setBlePin(uint32_t pin) {
  if (!_prefs) return false;
  if (pin != 0 && isWeakBlePin(pin)) return false;  // 0 = random PIN at every boot
  _prefs->ble_pin = pin;
  if (pin != 0) the_mesh.setBLEPin(pin);
  the_mesh.savePrefs();
  _store.status().ble_pin = pin;
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setRepeatEnabled(bool enabled) {
  if (!_prefs) return false;
  RadioConfig cfg = _store.radio();
  cfg.client_repeat = enabled;
  if (enabled && !RadioConfig::isAllowedRepeatFrequency(cfg.freq_mhz)) {
    // Move to a repeat frequency only if the current bandwidth still fits the band there
    cfg.freq_mhz = 869.525f;
    if (!RadioBandPolicy::channelInBand(cfg.freq_mhz, cfg.bw_khz)) return false;
  }
  return setRadioConfig(cfg);
}

bool DeviceMeshActions::setDefaultScope(const char* scope_name) {
  if (!_prefs || !scope_name) return false;
  strncpy(_prefs->default_scope_name, scope_name, sizeof(_prefs->default_scope_name) - 1);
  the_mesh.savePrefs();
  strncpy(_store.status().active_scope_name, scope_name, sizeof(_store.status().active_scope_name) - 1);
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setGpsConfig(bool enabled, uint32_t baud_rate, uint32_t interval_secs,
                                     bool share_loc_in_advert) {
  if (!_prefs) return false;
  _prefs->gps_enabled = enabled ? 1 : 0;
  _prefs->gps_interval = interval_secs;
  _prefs->advert_loc_policy = share_loc_in_advert ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  the_mesh.savePrefs();
#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif
  _store.gps().enabled = enabled;
  _store.gps().baud_rate = baud_rate;
  _store.bumpVersion();
  return true;
}


bool DeviceMeshActions::exportIdentityHex(char* out_hex, size_t max_len) {
  // Full mesh::LocalIdentity is 96 bytes (64B prv_key || 32B pub_key) -> 192 hex chars + NUL
  if (!out_hex || max_len < 193) return false;
  uint8_t raw96[96] = {0};
  if (the_mesh.self_id.writeTo(raw96, sizeof(raw96)) != sizeof(raw96)) {
    memset(raw96, 0, sizeof(raw96));
    return false;
  }
  AppStore::bytesToHex(raw96, sizeof(raw96), out_hex, max_len);
  memset(raw96, 0, sizeof(raw96));
  return true;
}

// Only reachable from the on-device Settings > SD page after a confirmation tap.
bool DeviceMeshActions::exportIdentityToSd() {
  if (!_sd || !_sd->isMounted()) return false;
  uint8_t raw96[96] = {0};
  if (the_mesh.self_id.writeTo(raw96, sizeof(raw96)) != sizeof(raw96)) {
    memset(raw96, 0, sizeof(raw96));
    return false;
  }
  bool ok = _sd->writeFile("/faro/identity.bak", raw96, sizeof(raw96));
  memset(raw96, 0, sizeof(raw96));
  return ok;
}

bool DeviceMeshActions::importIdentityHex(const char* hex_str) {
  uint8_t raw96[96] = {0};
  size_t decoded_len = 0;

  if (hex_str && strcmp(hex_str, "SD") == 0) {
    if (!_sd) {
      return false;
    }
    int32_t n = _sd->readFile("/faro/identity.bak", raw96, sizeof(raw96));
    if (n == 96 || n == 64) {
      decoded_len = size_t(n);
    } else {
      // Also support plain-text hex file /faro/prv_key.txt (128 or 192 hex chars) on MicroSD
      uint8_t txt_buf[200] = {0};
      int32_t tn = _sd->readFile("/faro/prv_key.txt", txt_buf, sizeof(txt_buf) - 1);
      if (tn >= 128) {
        // Strip trailing CR/LF/whitespace
        while (tn > 0 && (txt_buf[tn - 1] == '\r' || txt_buf[tn - 1] == '\n' || txt_buf[tn - 1] == ' ')) {
          txt_buf[--tn] = 0;
        }
        if ((tn == 128 || tn == 192) &&
            AppStore::hexToBytes(reinterpret_cast<const char*>(txt_buf), raw96, size_t(tn / 2))) {
          decoded_len = size_t(tn / 2);
        }
      }
      memset(txt_buf, 0, sizeof(txt_buf));
    }
  } else if (hex_str) {
    size_t slen = strlen(hex_str);
    if ((slen == 128 || slen == 192) && AppStore::hexToBytes(hex_str, raw96, slen / 2)) {
      decoded_len = slen / 2;
    }
  }

  if (decoded_len != 64 && decoded_len != 96) {
    memset(raw96, 0, sizeof(raw96));
    return false;
  }

  // Validate 64B private key (and 32B public key if 96B provided)
  if (isAllZero(raw96, 64) ||
      (decoded_len == 96 && isAllZero(raw96 + 64, 32)) ||
      !mesh::LocalIdentity::validatePrivateKey(raw96)) {
    memset(raw96, 0, sizeof(raw96));
    return false;
  }

  mesh::LocalIdentity identity;
  // When decoded_len == 64, LocalIdentity::readFrom automatically derives the 32B pub_key via Ed25519
  identity.readFrom(raw96, decoded_len);
  memset(raw96, 0, sizeof(raw96));

  if (!::store.saveMainIdentity(identity)) {
    return false;
  }
  // Contacts hold ECDH secrets derived from the old key: restart so everything is re-derived
  // from the new identity (upstream's import resets contacts for the same reason).
  _reboot_at_ms = millis() + 2000;
  if (_reboot_at_ms == 0) _reboot_at_ms = 1;
  snprintf(_store.status().sd_backup_status, sizeof(_store.status().sd_backup_status),
           "Identity imported - restarting...");
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::factoryReset() {
  if (_ifaces) {
    _ifaces->disable();
  }
  bool ok = ::store.formatFileSystem();
  if (ok && _board) {
    delay(250);
    _board->reboot();
  }
  return ok;
}

void DeviceMeshActions::rebootToDfu() {
  if (_board) _board->reboot();
}

bool DeviceMeshActions::runCliCommand(const char* cmd, char* out_reply, size_t max_len) {
  if (!cmd || !cmd[0]) return false;
  char reply[256] = {0};
  bool handled = false;
  if (_prefs && _prefs->getRadioPrefs()->handleCommand(cmd, 0, reply)) {
    if (_prefs->getRadioPrefs()->isDirty()) the_mesh.savePrefs();
    handled = true;
  } else if (_board && _board->handleCommand(cmd, 0, reply)) {
    if (_prefs && _prefs->isDirty()) the_mesh.savePrefs();
    handled = true;
  } else if (strcmp(cmd, "ver") == 0) {
    snprintf(reply, sizeof(reply), "> MeshCore v1.18.0-faro (%s)", _store.status().node_name);
    handled = true;
  } else if (strcmp(cmd, "help") == 0) {
    snprintf(reply, sizeof(reply),
             "> Commands: ver, get name, set name <x>, set prv.key <hex>, export settings, import settings, clock, reboot");
    handled = true;
  } else if (strncmp(cmd, "set name ", 9) == 0) {
    if (setNodeName(cmd + 9)) {
      snprintf(reply, sizeof(reply), "> Node name saved: %s", _store.status().node_name);
    } else {
      snprintf(reply, sizeof(reply), "> Error setting node name");
    }
    handled = true;
  } else if (strncmp(cmd, "set prv.key ", 12) == 0) {
    if (importIdentityHex(cmd + 12)) {
      char pub_hex[20] = {0};
      AppStore::bytesToHex(_store.status().self_pubkey, 8, pub_hex, sizeof(pub_hex));
      snprintf(reply, sizeof(reply), "> Identity imported OK! PubKey: %s...", pub_hex);
    } else {
      snprintf(reply, sizeof(reply), "> Error: invalid key (provide 128-hex prv_key or 192-hex prv+pub)");
    }
    handled = true;
  } else if (strcmp(cmd, "get prv.key") == 0 || strcmp(cmd, "export sd") == 0) {
    snprintf(reply, sizeof(reply), "> Key export only via Settings > SD (needs confirmation on screen)");
    handled = true;
  } else if (strcmp(cmd, "import sd") == 0) {
    if (importIdentityHex("SD")) {
      char pub_hex[20] = {0};
      AppStore::bytesToHex(_store.status().self_pubkey, 8, pub_hex, sizeof(pub_hex));
      snprintf(reply, sizeof(reply), "> Restored from SD (/faro/identity.bak or prv_key.txt) Pub:%s...", pub_hex);
    } else {
      snprintf(reply, sizeof(reply), "> Error: SD /faro/identity.bak or /faro/prv_key.txt not found/invalid");
    }
    handled = true;
  } else if (strcmp(cmd, "export settings") == 0) {
    if (exportSettingsToSd(true)) {
      snprintf(reply, sizeof(reply), "> %s", _store.status().sd_backup_status);
    } else {
      snprintf(reply, sizeof(reply), "> Error: %s", _store.status().sd_backup_status);
    }
    handled = true;
  } else if (strcmp(cmd, "import settings") == 0) {
    if (importSettingsFromSd()) {
      snprintf(reply, sizeof(reply), "> %s", _store.status().sd_backup_status);
    } else {
      snprintf(reply, sizeof(reply), "> Error: %s", _store.status().sd_backup_status);
    }
    handled = true;
  } else if (strcmp(cmd, "get name") == 0) {
    snprintf(reply, sizeof(reply), "> %s", _store.status().node_name);
    handled = true;
  } else if (strcmp(cmd, "clock") == 0) {
    uint32_t ts = the_mesh.getRTCClock() ? the_mesh.getRTCClock()->getCurrentTime() : 0;
    snprintf(reply, sizeof(reply), "> epoch=%u", unsigned(ts));
    handled = true;
  }
  if (!handled) {
    snprintf(reply, sizeof(reply), "Unknown command: %s", cmd);
  }
  // Never echo key material into the on-screen log
  const char* shown_cmd = (strncmp(cmd, "set prv.key", 11) == 0) ? "set prv.key <redacted>" : cmd;
  snprintf(_store.status().last_cli_reply, sizeof(_store.status().last_cli_reply),
           "> %s\n%s", shown_cmd, reply);
  if (out_reply && max_len > 0) {
    strncpy(out_reply, _store.status().last_cli_reply, max_len - 1);
    out_reply[max_len - 1] = '\0';
  }
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) {
  if (!target_pubkey || !password) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint32_t est_timeout = 0;
  int res = the_mesh.sendLogin(*ci, password, est_timeout);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGIN] %s (timeout=%ums)", (res == MSG_SEND_FAILED) ? "Failed" : "Sent", unsigned(est_timeout));
  _store.bumpVersion();
  return res != MSG_SEND_FAILED;
}

bool DeviceMeshActions::sendRemoteLogout(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  _store.status().remote_logged_in = false;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGOUT] Disconnected admin session.");
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::requestRemoteStatus(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint32_t tag = 0, est_timeout = 0;
  return the_mesh.sendRequest(*ci, REQ_TYPE_GET_STATUS, tag, est_timeout) != MSG_SEND_FAILED;
}

bool DeviceMeshActions::sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) {
  if (!target_pubkey || !cli_cmd || !cli_cmd[0]) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  uint32_t crc = 0, est_timeout = 0;
  int res = the_mesh.sendMessage(*ci, ts, 1 /* TXT_TYPE_CLI_DATA */, cli_cmd, crc, est_timeout);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[REMOTE CLI] > %s (%s)", cli_cmd, (res == MSG_SEND_FAILED) ? "ERR" : "Sent");
  _store.bumpVersion();
  return res != MSG_SEND_FAILED;
}

bool DeviceMeshActions::shareContactZeroHop(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  return the_mesh.shareContactZeroHop(*ci);
}

bool DeviceMeshActions::findPath(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(target_pubkey, PUB_KEY_SIZE);
  if (!ci) return false;
  uint8_t save = ci->out_path_len;
  ci->out_path_len = OUT_PATH_UNKNOWN;
  uint32_t tag = 0, est_timeout = 0;
  int res = the_mesh.sendRequest(*ci, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
  ci->out_path_len = save;
  return res != MSG_SEND_FAILED;
}

bool DeviceMeshActions::syncDeviceTime(uint32_t unix_ts) {
  if (_prefs) {
    _prefs->tz_offset = _store.status().tz_offset_hours;
    the_mesh.savePrefs();
  }
  if (unix_ts > 0 && the_mesh.getRTCClock()) {
    the_mesh.getRTCClock()->setCurrentTime(unix_ts);
    return true;
  }
  return true;
}

bool DeviceMeshActions::setTuningParams(float rx_delay, uint8_t interference_db, uint8_t agc_sec, bool double_ack) {
  auto& s = _store.status();
  s.rx_delay = rx_delay;
  s.interference_db = interference_db;
  s.agc_reset_sec = agc_sec;
  s.double_acks = double_ack;
  if (_prefs) {
    _prefs->rx_delay_base = rx_delay;
    _prefs->interference_threshold = interference_db;
    _prefs->agc_reset_interval = uint8_t(agc_sec / 4);
    _prefs->multi_acks = s.multi_acks;
    _prefs->cad_enabled = s.cad_enabled ? 1 : 0;
    _prefs->tx_delay_factor = s.tx_delay_factor;
    _prefs->direct_tx_delay_factor = s.direct_tx_delay_factor;
    _prefs->buzzer_quiet = _store.uiPrefs().quiet_mode ? 1 : 0;
    the_mesh.savePrefs();
  }
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) {
  auto& s = _store.status();
  s.telemetry_perm = telemetry_perm % 3;
  s.loc_policy = loc_policy % 3;
  if (_prefs) {
    _prefs->telemetry_mode_base = s.telemetry_perm;
    _prefs->telemetry_mode_loc = s.telemetry_loc_perm % 3;
    _prefs->telemetry_mode_env = s.telemetry_env_perm % 3;
    _prefs->advert_loc_policy = (s.loc_policy == 0) ? ADVERT_LOC_NONE : ADVERT_LOC_SHARE;
    if (s.loc_policy == 1) {
      _prefs->node_lat = s.manual_lat_deg;
      _prefs->node_lon = s.manual_lon_deg;
    }
    the_mesh.savePrefs();
  }
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setAutoAddConfig(bool enabled, uint8_t max_hops) {
  auto& s = _store.status();
  s.auto_add_contacts = enabled;
  s.auto_add_max_hops = max_hops;
  if (_prefs) {
    _prefs->manual_add_contacts = enabled ? 0 : 1;
    _prefs->autoadd_config = enabled ? 0xFF : 0x00;
    _prefs->autoadd_max_hops = max_hops;
    the_mesh.savePrefs();
  }
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::setPathHashMode(uint8_t hash_bytes) {
  if (!_prefs || hash_bytes < 1 || hash_bytes > 3) return false;
  _prefs->path_hash_mode = hash_bytes - 1;
  the_mesh.savePrefs();
  _store.radio().path_hash_mode = hash_bytes - 1;
  _store.status().path_hash_bytes = hash_bytes;
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::rebootRadio() {
  if (_board) _board->reboot();
  return true;
}

bool DeviceMeshActions::exportSettingsToSd(bool overwrite) {
  (void)overwrite;
  auto& st = _store.status();
  const auto& r = _store.radio();
  const auto& up = _store.uiPrefs();
  if (!_sd) {
    snprintf(st.sd_backup_status, sizeof(st.sd_backup_status), "ERR: SD driver not initialized.");
    _store.bumpVersion();
    return false;
  }

  FaroSdSettingsSnapshot snap{};
  strncpy(snap.node_name, st.node_name, sizeof(snap.node_name) - 1);
  snap.radio = r;
  snap.telemetry_perm = st.telemetry_perm;
  snap.telemetry_loc_perm = st.telemetry_loc_perm;
  snap.telemetry_env_perm = st.telemetry_env_perm;
  snap.loc_policy = st.loc_policy;
  snap.auto_advert = st.auto_advert;
  snap.auto_add_contacts = st.auto_add_contacts;
  snap.auto_add_max_hops = st.auto_add_max_hops;
  snap.path_hash_bytes = st.path_hash_bytes;
  snap.multi_acks = st.multi_acks;
  snap.cad_enabled = st.cad_enabled;
  snap.rx_delay = st.rx_delay;
  snap.tx_delay_factor = st.tx_delay_factor;
  snap.direct_tx_delay_factor = st.direct_tx_delay_factor;
  snap.interference_db = st.interference_db;
  snap.agc_reset_sec = st.agc_reset_sec;
  snap.ble_pin = 0;          // secrets (BLE PIN, Wi-Fi password) never go to the card
  snap.wifi_enabled = st.wifi_enabled;
  strncpy(snap.wifi_ssid, st.wifi_ssid, sizeof(snap.wifi_ssid) - 1);
  snap.gps_enabled = _store.gps().enabled;
  snap.gps_baud = _store.gps().baud_rate;
  snap.tz_offset_hours = st.tz_offset_hours;
  strncpy(snap.active_scope_name, st.active_scope_name, sizeof(snap.active_scope_name) - 1);
  snap.manual_lat_deg = st.manual_lat_deg;
  snap.manual_lon_deg = st.manual_lon_deg;
  snap.language = uint8_t(up.language);
  snap.outdoor_high_contrast = up.outdoor_high_contrast;
  snap.screen_timeout_secs = up.screen_timeout_secs;
  snap.quiet_mode = up.quiet_mode;
  snap.coord_format = up.coord_format;
  snap.store_and_forward = up.store_and_forward;
  snap.bot_mode = up.bot_mode;
  snap.kid_mode = up.kid_mode;

  if (!_sd->writeFile("/faro/settings.bin", reinterpret_cast<const uint8_t*>(&snap), sizeof(snap))) {
    snprintf(st.sd_backup_status, sizeof(st.sd_backup_status), "ERR: Check MicroSD card (FAT32).");
    _store.bumpVersion();
    return false;
  }

  // Also write human-readable JSON summary to /faro/settings.json (overwriting existing)
  char json_buf[640] = {0};
  int jlen = snprintf(json_buf, sizeof(json_buf),
                      "{\n"
                      "  \"node_name\": \"%s\",\n"
                      "  \"freq_mhz\": %.3f,\n"
                      "  \"bw_khz\": %.2f,\n"
                      "  \"sf\": %u,\n"
                      "  \"cr\": %u,\n"
                      "  \"tx_power_dbm\": %d,\n"
                      "  \"rx_boosted_gain\": %s,\n"
                      "  \"client_repeat\": %s,\n"
                      "  \"path_hash_bytes\": %u,\n"
                      "  \"telemetry_base\": %u,\n"
                      "  \"telemetry_loc\": %u,\n"
                      "  \"telemetry_env\": %u,\n"
                      "  \"loc_policy\": %u,\n"
                      "  \"auto_add_contacts\": %s,\n"
                      "  \"auto_add_max_hops\": %u,\n"
                      "  \"multi_acks\": %u,\n"
                      "  \"cad_enabled\": %s,\n"
                      "  \"wifi_ssid\": \"%s\",\n"
                      "  \"gps_enabled\": %s,\n"
                      "  \"tz_offset_hours\": %d,\n"
                      "  \"scope\": \"%s\"\n"
                      "}\n",
                      snap.node_name, double(snap.radio.freq_mhz), double(snap.radio.bw_khz),
                      unsigned(snap.radio.sf), unsigned(snap.radio.cr), int(snap.radio.tx_power_dbm),
                      snap.radio.rx_boosted_gain ? "true" : "false",
                      snap.radio.client_repeat ? "true" : "false",
                      unsigned(snap.path_hash_bytes),
                      unsigned(snap.telemetry_perm), unsigned(snap.telemetry_loc_perm), unsigned(snap.telemetry_env_perm),
                      unsigned(snap.loc_policy),
                      snap.auto_add_contacts ? "true" : "false", unsigned(snap.auto_add_max_hops),
                      unsigned(snap.multi_acks), snap.cad_enabled ? "true" : "false",
                      snap.wifi_ssid,
                      snap.gps_enabled ? "true" : "false",
                      int(snap.tz_offset_hours), snap.active_scope_name);
  if (jlen > 0) {
    _sd->writeFile("/faro/settings.json", reinterpret_cast<const uint8_t*>(json_buf), size_t(jlen));
  }

  st.sd_mounted = _sd->isMounted();
  st.sd_total_mb = _sd->totalMB();
  st.sd_used_mb = _sd->usedMB();
  snprintf(st.sd_backup_status, sizeof(st.sd_backup_status),
           "OK: Overwrote /faro/settings.bin & .json on SD!");
  _store.bumpVersion();
  return true;
}

bool DeviceMeshActions::importSettingsFromSd() {
  auto& st = _store.status();
  if (!_sd) {
    snprintf(st.sd_backup_status, sizeof(st.sd_backup_status), "ERR: SD driver not initialized.");
    _store.bumpVersion();
    return false;
  }

  FaroSdSettingsSnapshot snap{};
  int32_t n = _sd->readFile("/faro/settings.bin", reinterpret_cast<uint8_t*>(&snap), sizeof(snap));
  if (n != int32_t(sizeof(snap)) || snap.magic != 0x46534431U) {
    snprintf(st.sd_backup_status, sizeof(st.sd_backup_status),
             "ERR: /faro/settings.bin not found on SD.");
    _store.bumpVersion();
    return false;
  }

  // The file is untrusted input: terminate every string before use
  snap.node_name[sizeof(snap.node_name) - 1] = '\0';
  snap.wifi_ssid[sizeof(snap.wifi_ssid) - 1] = '\0';
  snap.wifi_pwd[sizeof(snap.wifi_pwd) - 1] = '\0';
  snap.active_scope_name[sizeof(snap.active_scope_name) - 1] = '\0';

  // Apply imported settings to both AppStore and MeshCore NodePrefs + hardware drivers.
  // setRadioConfig() applies the band policy, so a file cannot set an out-of-band radio.
  setNodeName(snap.node_name);
  setRadioConfig(snap.radio);
  st.telemetry_loc_perm = snap.telemetry_loc_perm % 3;
  st.telemetry_env_perm = snap.telemetry_env_perm % 3;
  st.manual_lat_deg = snap.manual_lat_deg;
  st.manual_lon_deg = snap.manual_lon_deg;
  setTelemetryPerms(snap.telemetry_perm, snap.loc_policy);
  st.auto_advert = snap.auto_advert;
  setAutoAddConfig(snap.auto_add_contacts, snap.auto_add_max_hops);
  setPathHashMode(snap.path_hash_bytes);
  st.multi_acks = snap.multi_acks;
  st.cad_enabled = snap.cad_enabled;
  st.tx_delay_factor = snap.tx_delay_factor;
  st.direct_tx_delay_factor = snap.direct_tx_delay_factor;
  setTuningParams(snap.rx_delay, snap.interference_db, snap.agc_reset_sec, snap.multi_acks > 0);
  if (snap.ble_pin != 0) setBlePin(snap.ble_pin);  // older files; weak PINs are refused
  // Older files may carry a password; newer ones never do (keep the current one then)
  setWifiConfig(snap.wifi_enabled, snap.wifi_ssid, snap.wifi_pwd[0] ? snap.wifi_pwd : _store.status().wifi_pwd);
  memset(snap.wifi_pwd, 0, sizeof(snap.wifi_pwd));
  setGpsConfig(snap.gps_enabled, snap.gps_baud, 10, snap.loc_policy == 2);
  st.tz_offset_hours = snap.tz_offset_hours;
  syncDeviceTime(0);
  setDefaultScope(snap.active_scope_name);

  auto& up = _store.uiPrefs();
  up.language = static_cast<Language>(snap.language % 4);
  up.outdoor_high_contrast = snap.outdoor_high_contrast;
  up.screen_timeout_secs = snap.screen_timeout_secs;
  up.quiet_mode = snap.quiet_mode;
  up.coord_format = snap.coord_format % 3;
  up.store_and_forward = snap.store_and_forward;
  up.bot_mode = snap.bot_mode % 3;
  up.kid_mode = snap.kid_mode;

  st.sd_mounted = _sd->isMounted();
  st.sd_total_mb = _sd->totalMB();
  st.sd_used_mb = _sd->usedMB();
  snprintf(st.sd_backup_status, sizeof(st.sd_backup_status),
           "OK: Imported settings from SD (%s)!", st.node_name);
  _store.bumpVersion();
  return true;
}

}  // namespace faro::platform
#endif
