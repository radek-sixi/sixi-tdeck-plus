// SPDX-License-Identifier: Apache-2.0
// sim/VirtualMeshActions.cpp — 6-node virtual Alpine mesh harness implementation
#include "VirtualMeshActions.h"
#include <stdio.h>
#include <string.h>

namespace faro::sim {

using namespace faro::core;

VirtualMeshActions::VirtualMeshActions(AppStore& store, MeshEventBus& bus)
  : _store(store), _bus(bus) {}

void VirtualMeshActions::seedDemoTopology() {
  // Self status & GPS fix (Zermatt / Matterhorn valley)
  SystemStatus& st = _store.status();
  strncpy(st.node_name, "Faro-TDeck-01", sizeof(st.node_name) - 1);
  for (int i = 0; i < 32; ++i) st.self_pubkey[i] = uint8_t(0x50 + i);
  st.batt_mv = 4085;
  st.batt_pct = 88;
  st.charging = false;
  st.ble_connected = true;
  st.ble_pin = 123456;
  st.wifi_enabled = true;
  st.wifi_connected = true;
  strncpy(st.wifi_ssid, "Alpine-MeshLAN", sizeof(st.wifi_ssid) - 1);
  strncpy(st.wifi_ip, "192.168.4.21", sizeof(st.wifi_ip) - 1);
  st.wifi_port = 5000;
  st.sd_mounted = true;
  st.sd_used_mb = 412;
  st.sd_total_mb = 16384;
  st.uptime_secs = 5420;
  st.free_heap = 148200;
  st.free_psram = 7864320;
  st.noise_floor_dbm = -115;
  st.last_rssi = -68;
  st.last_snr_q4 = 44;  // +11.0 dB
  st.tx_packets = 48;
  st.rx_packets = 192;
  st.flood_tx = 14;
  st.direct_tx = 34;
  st.queue_size = 0;
  strncpy(st.active_scope_name, "#valais-alps", sizeof(st.active_scope_name) - 1);
  st.duty_cycle_permille = 18;  // 1.8% of 10% limit

  GpsFix& g = _store.gps();
  g.valid = true;
  g.lat_deg = 46.0207;
  g.lon_deg = 7.7491;
  g.alt_m = 1620.0;
  g.satellites = 9;
  g.speed_kmh = 3.6f;
  g.course_deg = 142.0f;
  g.baud_rate = 38400;
  g.nmea_sentences = 3842;
  g.last_fix_ts = 1729416000;

  // 6 virtual nodes
  struct SeedNode {
    uint8_t  key0;
    const char* name;
    NodeRole role;
    uint8_t  path_len;
    int32_t  lat_e6;
    int32_t  lon_e6;
    int8_t   snr_q4;
    int16_t  rssi;
    bool     fav;
  };
  static const SeedNode kNodes[6] = {
    {0xA1, "Gnifetti Hut",        NodeRole::ChatNode,   1, 45901200, 7871100, 38, -74, true},
    {0xB2, "Guide Marco",         NodeRole::ChatNode,   2, 45976400, 7658600, 24, -84, true},
    {0xC3, "Monte Rosa Repeater", NodeRole::Repeater,   0, 45936900, 7866800, 44, -68, true},
    {0xD4, "Gornergrat Relay",    NodeRole::Repeater,   0, 45983600, 7784700, 29, -81, false},
    {0xE5, "Capanna Margherita",  NodeRole::RoomServer, 1, 45927200, 7876700, 32, -79, false},
    {0xF6, "Zermatt Meteo Sens",  NodeRole::Sensor,     0, 46018000, 7752000, 50, -64, false}
  };

  for (int i = 0; i < 6; ++i) {
    ContactRecord c{};
    for (int b = 0; b < 32; ++b) c.pub_key[b] = uint8_t(kNodes[i].key0 + b);
    strncpy(c.name, kNodes[i].name, sizeof(c.name) - 1);
    c.role = kNodes[i].role;
    c.out_path_len = kNodes[i].path_len;
    c.out_path[0] = 0xC3;
    c.out_path[1] = 0xD4;
    c.last_advert_ts = 1729415900 + i * 10;
    c.lastmod_ts = c.last_advert_ts;
    c.gps_lat_e6 = kNodes[i].lat_e6;
    c.gps_lon_e6 = kNodes[i].lon_e6;
    c.last_snr_q4 = kNodes[i].snr_q4;
    c.last_rssi = kNodes[i].rssi;
    c.favourite = kNodes[i].fav;
    c.valid = true;
    _store.upsertContact(c);

    if (c.role == NodeRole::Repeater || c.role == NodeRole::RoomServer) {
      _store.repeatRoam().onRepeaterHeard(c.pub_key, c.name, c.last_snr_q4, c.last_rssi,
                                          c.gps_lat_e6, c.gps_lon_e6, 1000);
    }
  }
  uint8_t pref[32] = {0};
  _store.repeatRoam().evaluate(1000, pref);

  // Additional channels
  addHashtagChannel("#valais-rescue");
  addHashtagChannel("#meteo-alpi");

  // Seed DM conversation with Gnifetti Hut (0xA1..)
  uint8_t gnifetti_prefix[6] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};
  {
    MessageRecord m1{};
    memcpy(m1.peer_prefix, gnifetti_prefix, 6);
    strncpy(m1.sender_name, "Gnifetti", sizeof(m1.sender_name) - 1);
    strncpy(m1.text, "Lys glacier open; wind 35km/h.", sizeof(m1.text) - 1);
    m1.timestamp = 1729415400;
    m1.delivery_state = DeliveryState::DeliveredAck;
    m1.hops = 1;
    m1.snr_q4 = 38;
    m1.rssi = -74;
    m1.outgoing = false;
    _store.appendMessage(m1);

    MessageRecord m2{};
    memcpy(m2.peer_prefix, gnifetti_prefix, 6);
    strncpy(m2.sender_name, "Faro-TDeck-01", sizeof(m2.sender_name) - 1);
    strncpy(m2.text, "Copy! Team of 4 via Mantova, ETA 14:30.", sizeof(m2.text) - 1);
    m2.timestamp = 1729415520;
    m2.ack_crc = 0xABCDEF01;
    m2.delivery_state = DeliveryState::DeliveredAck;
    m2.hops = 1;
    m2.snr_q4 = 40;
    m2.rssi = -72;
    m2.outgoing = true;
    _store.appendMessage(m2);

    MessageRecord m3{};
    memcpy(m3.peer_prefix, gnifetti_prefix, 6);
    strncpy(m3.sender_name, "Gnifetti", sizeof(m3.sender_name) - 1);
    strncpy(m3.text, "Hot soup ready on 869.618MHz.", sizeof(m3.text) - 1);
    m3.timestamp = 1729415600;
    m3.delivery_state = DeliveryState::DeliveredAck;
    m3.hops = 1;
    m3.snr_q4 = 38;
    m3.rssi = -74;
    m3.outgoing = false;
    _store.appendMessage(m3);
  }
  if (ContactRecord* gn = _store.findContactByPrefixMutable(gnifetti_prefix)) {
    gn->unread_count = 1;
  }

  // Seed Channel 0 (Public) & Channel 1 (#valais-rescue) messages
  {
    MessageRecord cm1{};
    cm1.is_channel = true;
    cm1.channel_index = 0;
    strncpy(cm1.sender_name, "Guida Alpina Marco", sizeof(cm1.sender_name) - 1);
    strncpy(cm1.text, "Good morning mesh! Clear skies at Hörnlihütte 3260m.", sizeof(cm1.text) - 1);
    cm1.timestamp = 1729415100;
    cm1.hops = 2;
    cm1.snr_q4 = 24;
    cm1.rssi = -84;
    _store.appendMessage(cm1);

    MessageRecord cm2{};
    cm2.is_channel = true;
    cm2.channel_index = 1;
    strncpy(cm2.sender_name, "Capanna Margherita", sizeof(cm2.sender_name) - 1);
    strncpy(cm2.text, "[METEO] High altitude gusts expected after 16:00 UTC.", sizeof(cm2.text) - 1);
    cm2.timestamp = 1729415700;
    cm2.hops = 1;
    cm2.snr_q4 = 32;
    cm2.rssi = -79;
    _store.appendMessage(cm2);
  }

  // Seed Trace Path Result
  TraceResult& tr = _store.lastTrace();
  tr.tag = 402;
  tr.auth_code = 0x99887766;
  tr.flags = 1;
  tr.hop_count = 3;
  tr.hops[0] = {0xD4, 36};  // Gornergrat Relay (+9.0 dB)
  tr.hops[1] = {0xC3, 44};  // Monte Rosa Repeater (+11.0 dB)
  tr.hops[2] = {0xA1, 38};  // Gnifetti Hut (+9.5 dB)
  tr.timestamp = 1729415850;
  tr.valid = true;

}

void VirtualMeshActions::tick(uint32_t now_ms) {
  _store.tickMessageTimeouts(now_ms);
  for (uint32_t id = 0; _store.takeResendRequest(id);) resendDirectMessage(id);
}

bool VirtualMeshActions::resendDirectMessage(uint32_t msg_id) {
  MessageRecord* m = _store.findMessageByIdMutable(msg_id);
  if (!m || !m->outgoing || m->is_channel) return false;
  m->ack_crc = ++_next_crc;
  m->delivery_state = DeliveryState::SentFlood;
  _store.status().tx_packets++;
  MeshEvent ack{};
  ack.type = MeshEventType::AckRecv;
  ack.ack_crc = m->ack_crc;
  _bus.push(ack);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                                           uint32_t& out_ack_crc, DeliveryState& out_state) {
  if (!peer_pubkey || !text || !text[0]) return false;
  out_ack_crc = ++_next_crc;
  const ContactRecord* c = _store.findContactByPubKey(peer_pubkey);
  out_state = (c && c->out_path_len < 64) ? DeliveryState::SentDirect : DeliveryState::SentFlood;

  MessageRecord m{};
  m.is_channel = false;
  memcpy(m.peer_prefix, peer_pubkey, 6);
  strncpy(m.sender_name, _store.status().node_name, sizeof(m.sender_name) - 1);
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.timestamp = 1729416100;
  m.ack_crc = out_ack_crc;
  m.delivery_state = out_state;
  m.hops = (c && c->out_path_len < 64) ? c->out_path_len : 0;
  m.outgoing = true;
  _store.appendMessage(m);
  _store.status().tx_packets++;
  if (out_state == DeliveryState::SentDirect) {
    _store.status().direct_tx++;
  } else {
    _store.status().flood_tx++;
  }

  // Push immediate simulated ACK onto EventBus
  MeshEvent ack{};
  ack.type = MeshEventType::AckRecv;
  ack.ack_crc = out_ack_crc;
  _bus.push(ack);
  return true;
}

bool VirtualMeshActions::sendChannelMessage(uint8_t channel_index, const char* text) {
  if (!text || !text[0]) return false;
  MessageRecord m{};
  m.is_channel = true;
  m.channel_index = channel_index;
  strncpy(m.sender_name, _store.status().node_name, sizeof(m.sender_name) - 1);
  strncpy(m.text, text, sizeof(m.text) - 1);
  m.timestamp = 1729416100;
  m.delivery_state = DeliveryState::SentFlood;
  m.outgoing = true;
  _store.appendMessage(m);
  _store.status().tx_packets++;
  _store.status().flood_tx++;
  return true;
}

bool VirtualMeshActions::sendZeroHopAdvert() {
  _zero_hop_adverts++;
  _store.status().tx_packets++;
  _store.status().direct_tx++;
  return true;
}

bool VirtualMeshActions::sendFloodAdvert() {
  _flood_adverts++;
  _store.status().tx_packets++;
  _store.status().flood_tx++;
  return true;
}

void VirtualMeshActions::syncStore() {}

bool VirtualMeshActions::addOrUpdateContact(const ContactRecord& contact) {
  return _store.upsertContact(contact) != nullptr;
}

bool VirtualMeshActions::removeContact(const uint8_t pub_key[32]) {
  return _store.removeContact(pub_key);
}

bool VirtualMeshActions::resetContactPath(const uint8_t pub_key[32]) {
  ContactRecord* c = _store.findContactByPubKeyMutable(pub_key);
  if (!c) return false;
  c->out_path_len = 0xFF;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setPreferredRepeater(const uint8_t repeater_pubkey[32]) {
  if (!repeater_pubkey) return false;
  _store.repeatRoam().onRepeaterHeard(repeater_pubkey, "Preferred Repeater",
                                      48, -65, 0, 0, 50000);
  uint8_t out[32] = {0};
  _store.repeatRoam().evaluate(50000, out);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) {
  if (!name || !name[0] || !secret) return false;
  ChannelRecord ch{};
  ch.index = uint8_t(_store.channelCount());
  strncpy(ch.name, name, sizeof(ch.name) - 1);
  ch.secret_len = (secret_len == 32) ? 32 : 16;
  memcpy(ch.secret, secret, ch.secret_len);
  ch.is_hashtag = (name[0] == '#');
  ch.valid = true;
  return _store.upsertChannel(ch) != nullptr;
}

bool VirtualMeshActions::addHashtagChannel(const char* hashtag_name) {
  if (!hashtag_name || !hashtag_name[0]) return false;
  ChannelRecord ch{};
  ch.index = uint8_t(_store.channelCount());
  if (hashtag_name[0] == '#') {
    strncpy(ch.name, hashtag_name, sizeof(ch.name) - 1);
  } else {
    snprintf(ch.name, sizeof(ch.name), "#%s", hashtag_name);
  }
  for (size_t i = 0; i < 16; ++i) {
    ch.secret[i] = uint8_t(ch.name[i % strlen(ch.name)] ^ (0x5A + i));
  }
  ch.secret_len = 16;
  ch.is_hashtag = true;
  ch.valid = true;
  return _store.upsertChannel(ch) != nullptr;
}

bool VirtualMeshActions::removeChannel(uint8_t channel_index) {
  return _store.removeChannel(channel_index);
}

bool VirtualMeshActions::sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) {
  if (!target_pubkey) return false;
  out_tag = ++_next_tag;
  MeshEvent ev{};
  ev.type = MeshEventType::TraceRecv;
  ev.trace.tag = out_tag;
  ev.trace.auth_code = 0xCAFE1234;
  ev.trace.hop_count = 2;
  ev.trace.hops[0] = {0xC3, 44};
  ev.trace.hops[1] = {target_pubkey[0], 36};
  ev.trace.timestamp = 1729416200;
  ev.trace.valid = true;
  _bus.push(ev);
  return true;
}

bool VirtualMeshActions::requestTelemetry(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  if (ContactRecord* c = _store.findContactByPubKeyMutable(target_pubkey)) {
    c->online = true;
    c->remote_batt_mv = 4120;
    c->remote_temp_c = 18;
  }
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[TELEMETRY] Online! Batt: 4.12V | Temp: 18.5C | GPS OK");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setRadioConfig(const RadioConfig& cfg) {
  const RadioConfig applied = RadioConfig::sanitized(cfg, _store.regionInfo());
  if (!RadioConfig::isValid(applied, _store.regionInfo())) return false;
  _store.radio() = applied;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setRegion(core::RadioRegionId id) {
  const core::RadioRegionId before = _store.region();
  if (!_store.setRegion(id)) return false;
  uint8_t hash_bytes = 0;
  const RadioConfig cfg = RadioConfig::regionDefault(_store.regionInfo(), _store.radio(), hash_bytes);
  if (!setRadioConfig(cfg)) {
    _store.setRegion(before);
    return false;
  }
  if (hash_bytes != 0) setPathHashMode(hash_bytes);
  return true;
}

bool VirtualMeshActions::exportContactsJson(char* out_path, size_t max_len) {
  const bool ok = _store.exportContactsJson("/tmp/faro_contacts.json", nullptr, 0);
  snprintf(_store.status().export_status, sizeof(_store.status().export_status),
           ok ? "Saved /tmp/faro_contacts.json" : "ERR: cannot write /tmp/faro_contacts.json");
  if (out_path && max_len > 0) snprintf(out_path, max_len, "%s", _store.status().export_status);
  _store.bumpVersion();
  return ok;
}

bool VirtualMeshActions::exportGpsTrackGpx(char* out_path, size_t max_len) {
  const bool ok = _store.exportGpsTrackGpx("/tmp/faro_track.gpx", nullptr, 0);
  snprintf(_store.status().export_status, sizeof(_store.status().export_status),
           ok ? "Saved /tmp/faro_track.gpx" : "ERR: cannot write /tmp/faro_track.gpx");
  if (out_path && max_len > 0) snprintf(out_path, max_len, "%s", _store.status().export_status);
  _store.bumpVersion();
  return ok;
}

bool VirtualMeshActions::scanNearbyNodes() {
  // Two demo repeaters answer the discover request
  _store.clearDiscovered();
  size_t answered = 0;
  for (size_t i = 0; i < core::AppStore::kMaxContacts && answered < 2; ++i) {
    const auto& c = _store.contacts()[i];
    if (!c.valid || c.role != core::NodeRole::Repeater) continue;
    core::MeshEvent ev{};
    ev.type = core::MeshEventType::NodeDiscovered;
    memcpy(ev.pub_key, c.pub_key, 8);
    ev.role = c.role;
    ev.snr_q4 = int8_t(24 - 8 * int(answered));
    ev.snr2_q4 = int8_t(20 - 6 * int(answered));
    ev.rssi = int16_t(-80 - 7 * int(answered));
    _bus.push(ev);
    ++answered;
  }
  return true;
}

bool VirtualMeshActions::setNodeName(const char* name) {
  if (!name || !name[0]) return false;
  strncpy(_store.status().node_name, name, sizeof(_store.status().node_name) - 1);
  _store.status().node_name[sizeof(_store.status().node_name) - 1] = '\0';
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setWifiConfig(bool enabled, const char* ssid, const char* password) {
  _store.status().wifi_enabled = enabled;
  if (ssid) {
    strncpy(_store.status().wifi_ssid, ssid, sizeof(_store.status().wifi_ssid) - 1);
  }
  if (password) {
    strncpy(_store.status().wifi_pwd, password, sizeof(_store.status().wifi_pwd) - 1);
  }
  if (enabled && _store.status().wifi_ssid[0]) {
    _store.status().wifi_connected = true;
    strncpy(_store.status().wifi_ip, "192.168.1.105", sizeof(_store.status().wifi_ip) - 1);
  } else if (!enabled) {
    _store.status().wifi_connected = false;
  }
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setBlePin(uint32_t pin) {
  if (pin < 100000 || pin > 999999) return false;
  _store.status().ble_pin = pin;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setRepeatEnabled(bool enabled) {
  RadioConfig cfg = _store.radio();
  cfg.client_repeat = enabled;
  return setRadioConfig(cfg);
}

bool VirtualMeshActions::setDefaultScope(const char* scope_name) {
  if (!scope_name) return false;
  strncpy(_store.status().active_scope_name, scope_name,
          sizeof(_store.status().active_scope_name) - 1);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setGpsConfig(bool enabled, uint32_t baud_rate, uint32_t interval_secs,
                                      bool share_loc_in_advert) {
  (void)interval_secs; (void)share_loc_in_advert;
  _store.gps().enabled = enabled;
  _store.gps().baud_rate = (baud_rate == 9600) ? 9600 : 38400;
  _store.bumpVersion();
  return true;
}


bool VirtualMeshActions::exportIdentityHex(char* out_hex, size_t max_len) {
  AppStore::bytesToHex(_store.status().self_pubkey, 32, out_hex, max_len);
  return true;
}

bool VirtualMeshActions::importIdentityHex(const char* hex_str) {
  return AppStore::hexToBytes(hex_str, _store.status().self_pubkey, 32);
}

bool VirtualMeshActions::factoryReset() {
  return true;
}

void VirtualMeshActions::rebootToDfu() {}

bool VirtualMeshActions::runCliCommand(const char* cmd, char* out_reply, size_t max_len) {
  if (!cmd) return false;
  snprintf(_store.status().last_cli_reply, sizeof(_store.status().last_cli_reply),
           "> %s\nMeshCore v1.18.0 OK (Node=%s, IP=%s, Freq=%.3f MHz)",
           cmd, _store.status().node_name, _store.status().wifi_ip,
           double(_store.radio().freq_mhz));
  if (out_reply && max_len > 0) {
    strncpy(out_reply, _store.status().last_cli_reply, max_len - 1);
    out_reply[max_len - 1] = '\0';
  }
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) {
  (void)target_pubkey; (void)password;
  _store.status().remote_logged_in = true;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGIN OK] Authenticated as Admin. Ready for CLI & stats.");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::sendRemoteLogout(const uint8_t target_pubkey[32]) {
  (void)target_pubkey;
  _store.status().remote_logged_in = false;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGOUT] Admin session closed.");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::requestRemoteStatus(const uint8_t target_pubkey[32]) {
  (void)target_pubkey;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[STATS] Up: 14d 6h | Batt: 4.14V | Noise: -115dBm | TX: 412 | RX: 1890");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::requestRemoteBinary(const uint8_t target_pubkey[32], const char* subcmd) {
  (void)target_pubkey;
  if (subcmd && strncmp(subcmd, "acl", 3) == 0) {
    snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
             "[ACL] 0x03 (Admin): Faro-TDeck | 0xC1 (Write+HiAlert): Gnifetti");
  } else if (subcmd && strncmp(subcmd, "mma", 3) == 0) {
    snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
             "[MMA 60m] Temp: min 12.1C / avg 15.4C / max 18.8C | Batt: 4.11V");
  } else {
    snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
             "[NEIGHBORS] C3:MonteRosa(+11dB) | D4:Gornergrat(+7dB) | A1:Gnifetti(+9dB)");
  }
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) {
  (void)target_pubkey;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[CLI[%s]] Reply: OK (v1.18.0)", cli_cmd ? cli_cmd : "");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setTuningParams(float rx_delay, uint8_t interference_db,
                                         uint8_t agc_sec, bool double_ack) {
  _store.status().rx_delay = rx_delay;
  _store.status().interference_db = interference_db;
  _store.status().agc_reset_sec = agc_sec;
  _store.status().double_acks = double_ack;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) {
  _store.status().telemetry_perm = telemetry_perm;
  _store.status().loc_policy = loc_policy;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setAutoAddConfig(bool enabled, uint8_t max_hops) {
  _store.status().auto_add_contacts = enabled;
  _store.status().auto_add_max_hops = max_hops;
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::setPathHashMode(uint8_t hash_bytes) {
  _store.status().path_hash_bytes = hash_bytes;
  _store.radio().path_hash_mode = uint8_t(hash_bytes - 1);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::useLastAdvertPath(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  if (core::ContactRecord* c = _store.findContactByPubKeyMutable(target_pubkey)) {
    c->out_path_len = core::ContactRecord::encodePathLen(1, 2);
    c->out_path[0] = 0xC3;
    c->out_path[1] = 0xD4;
    _store.bumpVersion();
    return true;
  }
  return false;
}

bool VirtualMeshActions::exportContactUri(const uint8_t target_pubkey[32],
                                          char* out_uri, size_t max_len) {
  if (!target_pubkey || !out_uri) return false;
  const core::ContactRecord* c = _store.findContactByPubKey(target_pubkey);
  if (!c) return false;
  if (!core::AppStore::formatContactUri(*c, out_uri, max_len)) return false;
  strncpy(_store.uiPrefs().clipboard, out_uri, sizeof(_store.uiPrefs().clipboard) - 1);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::importContactUri(const char* uri_or_hex) {
  if (!uri_or_hex) return false;
  core::ContactRecord c{};
  if (core::AppStore::parseContactUri(uri_or_hex, c)) {
    return addOrUpdateContact(c);
  }
  return false;
}

bool VirtualMeshActions::getCustomVars(char* out_vars, size_t max_len) {
  if (out_vars && max_len > 0) {
    strncpy(out_vars, _store.status().custom_vars_str, max_len - 1);
    out_vars[max_len - 1] = '\0';
  }
  return true;
}

bool VirtualMeshActions::setCustomVar(const char* key_val) {
  if (!key_val) return false;
  strncpy(_store.status().custom_vars_str, key_val, sizeof(_store.status().custom_vars_str) - 1);
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::exportSettingsToSd(bool overwrite) {
  (void)overwrite;
  auto& st = _store.status();
  const auto& r = _store.radio();
  const auto& up = _store.uiPrefs();
  core::FaroSdSettingsSnapshot snap{};
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
  snap.ble_pin = 0;
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

  FILE* fp = fopen("/tmp/faro_sd_settings.bin", "wb");
  if (!fp) return false;
  size_t w = fwrite(&snap, sizeof(snap), 1, fp);
  fclose(fp);
  if (w != 1) return false;
  snprintf(st.sd_backup_status, sizeof(st.sd_backup_status),
           "OK: Overwrote /faro/settings.bin & .json on SD!");
  _store.bumpVersion();
  return true;
}

bool VirtualMeshActions::importSettingsFromSd() {
  auto& st = _store.status();
  FILE* fp = fopen("/tmp/faro_sd_settings.bin", "rb");
  if (!fp) return false;
  core::FaroSdSettingsSnapshot snap{};
  size_t rd = fread(&snap, sizeof(snap), 1, fp);
  fclose(fp);
  if (rd != 1 || snap.magic != 0x46534431U) return false;

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
  if (snap.ble_pin != 0) setBlePin(snap.ble_pin);
  setWifiConfig(snap.wifi_enabled, snap.wifi_ssid, snap.wifi_pwd[0] ? snap.wifi_pwd : _store.status().wifi_pwd);
  setGpsConfig(snap.gps_enabled, snap.gps_baud, 10, snap.loc_policy == 2);
  st.tz_offset_hours = snap.tz_offset_hours;
  setDefaultScope(snap.active_scope_name);

  auto& up = _store.uiPrefs();
  up.language = static_cast<core::Language>(snap.language % 4);
  up.outdoor_high_contrast = snap.outdoor_high_contrast;
  up.screen_timeout_secs = snap.screen_timeout_secs;
  up.quiet_mode = snap.quiet_mode;
  up.coord_format = snap.coord_format % 3;
  up.store_and_forward = snap.store_and_forward;
  up.bot_mode = snap.bot_mode % 3;
  up.kid_mode = snap.kid_mode;

  snprintf(st.sd_backup_status, sizeof(st.sd_backup_status),
           "OK: Imported settings from SD (%s)!", st.node_name);
  _store.bumpVersion();
  return true;
}

}  // namespace faro::sim
