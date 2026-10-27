// SPDX-License-Identifier: Apache-2.0
// app/core/AppStore.cpp — Observable application state store implementation
#include "AppStore.h"
#include "GeoMath.h"
#include <stdio.h>
#include <string.h>

namespace faro::core {

namespace {
static constexpr const char* kDefaultCanned[] = {
  "OK / Copy that",
  "En route to refuge",
  "Arrived safely at position",
  "Need weather/trail update",
  "Battery low, switching to eco",
  "Send GPS coordinates",
  "All clear on this sector",
  "Stand by — checking radio link"
};

// Escape a string for a JSON string literal (quotes, backslashes, control characters).
void jsonEscape(const char* in, char* out, size_t max_len) {
  if (!out || max_len == 0) return;
  size_t o = 0;
  for (const char* p = in ? in : ""; *p && o + 1 < max_len; ++p) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    if (ch == '"' || ch == '\\') {
      if (o + 2 >= max_len) break;
      out[o++] = '\\';
      out[o++] = char(ch);
    } else if (ch < 0x20) {
      if (o + 6 >= max_len) break;
      o += size_t(snprintf(out + o, max_len - o, "\\u%04x", ch));
    } else {
      out[o++] = char(ch);
    }
  }
  out[o] = '\0';
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

AppStore::AppStore() {
  _repeat_roam.setEnabled(true);
  setCannedMessages(kDefaultCanned, sizeof(kDefaultCanned) / sizeof(kDefaultCanned[0]));

  // Default Channel 0 ("Public")
  ChannelRecord pub{};
  pub.index = 0;
  strncpy(pub.name, "Public", sizeof(pub.name) - 1);
  // Default MeshCore Public PSK (izOH6cXN6mrJ5e26oRXNcg==)
  static const uint8_t kPubKey[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
  };
  memcpy(pub.secret, kPubKey, 16);
  pub.secret_len = 16;
  pub.is_hashtag = false;
  pub.valid = true;
  _channels[0] = pub;

  // Default Regions & Off-Grid Profiles (Ripple v8.2+ & v9.1+)
  strncpy(_regions[0].name, "*", sizeof(_regions[0].name) - 1);
  _regions[0].is_home = true;
  _regions[0].valid = true;

  strncpy(_regions[1].name, "#eu-mesh", sizeof(_regions[1].name) - 1);
  _regions[1].valid = true;

  strncpy(_regions[2].name, "#ch-alps", sizeof(_regions[2].name) - 1);
  _regions[2].valid = true;

  strncpy(_regions[3].name, "offgrid", sizeof(_regions[3].name) - 1);
  _regions[3].freq_mhz = 869.618f;
  _regions[3].bw_khz = 62.5f;
  _regions[3].sf = 8;
  _regions[3].cr = 8;
  _regions[3].offgrid_repeat = true;
  _regions[3].valid = true;
}

void AppStore::applyEvent(const MeshEvent& ev, uint32_t now_ms) {
  switch (ev.type) {
    case MeshEventType::DirectMessageRecv: {
      if (ev.txt_type == 1) {
        // TXT_TYPE_CLI_DATA: a repeater / room server answering our remote CLI command, not a chat line
        snprintf(_status.remote_admin_reply, sizeof(_status.remote_admin_reply), "[CLI] %.20s:\n%s",
                 ev.name[0] ? ev.name : "node", ev.text);
        if (!_ui_prefs.sniffer_paused) {
          RxPacketLogEntry pl{};
          pl.timestamp_ms = now_ms;
          pl.snr_q4 = ev.snr_q4;
          pl.rssi = ev.rssi;
          pl.hops = ev.hops;
          pl.len_bytes = uint16_t(16 + strlen(ev.text));
          strncpy(pl.type_label, "CLI_RPLY", sizeof(pl.type_label) - 1);
          snprintf(pl.summary, sizeof(pl.summary), "from %.20s [%uh]", ev.name[0] ? ev.name : "node",
                   unsigned(ev.hops));
          appendPacketLog(pl);
        }
        ++_version;
        break;
      }
      ContactRecord c{};
      memcpy(c.pub_key, ev.pub_key, 32);
      strncpy(c.name, ev.name[0] ? ev.name : "Peer", sizeof(c.name) - 1);
      c.role = ev.role;
      c.last_advert_ts = ev.timestamp;
      c.lastmod_ts = ev.timestamp;
      c.last_snr_q4 = ev.snr_q4;
      c.last_rssi = ev.rssi;
      c.valid = true;
      ContactRecord* stored = upsertContact(c);
      if (stored) stored->unread_count++;

      MessageRecord m{};
      m.is_channel = false;
      memcpy(m.peer_prefix, ev.pub_key, 6);
      strncpy(m.sender_name, ev.name[0] ? ev.name : "Peer", sizeof(m.sender_name) - 1);
      strncpy(m.text, ev.text, sizeof(m.text) - 1);
      m.timestamp = ev.timestamp;
      m.sent_ms = now_ms;
      m.delivery_state = DeliveryState::DeliveredAck;
      m.hops = ev.hops;
      m.snr_q4 = ev.snr_q4;
      m.rssi = ev.rssi;
      m.outgoing = false;
      m.valid = true;
      appendMessage(m);
      _status.rx_packets++;
      _status.last_snr_q4 = ev.snr_q4;
      _status.last_rssi = ev.rssi;
      pushRfHistorySample(ev.rssi, _status.noise_floor_dbm);
      if (_ui_prefs.range_test_active && _gps.valid) {
        recordRangeSample(_gps.lat_deg, _gps.lon_deg, ev.snr_q4, ev.rssi, ev.hops, ev.timestamp);
      }
      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        pl.hops = ev.hops;
        pl.len_bytes = uint16_t(16 + strlen(ev.text));
        strncpy(pl.type_label, (ev.txt_type == 1) ? "CLI_DM" : "TXT_MSG", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "from %s [%uh] %.24s",
                 ev.name[0] ? ev.name : "Peer", unsigned(ev.hops), ev.text);
        pl.valid = true;
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::ChannelMessageRecv: {
      ChannelRecord* ch = nullptr;
      for (size_t i = 0; i < kMaxChannels; ++i) {
        if (_channels[i].valid && _channels[i].index == ev.channel_index) {
          ch = &_channels[i];
          break;
        }
      }
      if (!ch && ev.channel_index < kMaxChannels) {
        ChannelRecord nc{};
        nc.index = ev.channel_index;
        strncpy(nc.name, ev.name[0] ? ev.name : "Channel", sizeof(nc.name) - 1);
        nc.valid = true;
        ch = upsertChannel(nc);
      }
      if (ch) ch->unread_count++;

      MessageRecord m{};
      m.is_channel = true;
      m.channel_index = ev.channel_index;
      // MeshCore group text payload format is "<sender_name>: <message_body>"
      const char* sep = strstr(ev.text, ": ");
      if (sep != nullptr && sep > ev.text &&
          size_t(sep - ev.text) < sizeof(m.sender_name)) {
        size_t slen = size_t(sep - ev.text);
        memcpy(m.sender_name, ev.text, slen);
        m.sender_name[slen] = '\0';
        strncpy(m.text, sep + 2, sizeof(m.text) - 1);
      } else {
        strncpy(m.sender_name, ev.name[0] ? ev.name : "Peer", sizeof(m.sender_name) - 1);
        strncpy(m.text, ev.text, sizeof(m.text) - 1);
      }
      m.timestamp = ev.timestamp;
      m.sent_ms = now_ms;
      m.delivery_state = DeliveryState::DeliveredAck;
      m.hops = ev.hops;
      m.snr_q4 = ev.snr_q4;
      m.rssi = ev.rssi;
      m.outgoing = false;
      m.valid = true;
      appendMessage(m);
      _status.rx_packets++;
      _status.last_snr_q4 = ev.snr_q4;
      _status.last_rssi = ev.rssi;
      pushRfHistorySample(ev.rssi, _status.noise_floor_dbm);
      if (_ui_prefs.range_test_active && _gps.valid) {
        recordRangeSample(_gps.lat_deg, _gps.lon_deg, ev.snr_q4, ev.rssi, ev.hops, ev.timestamp);
      }
      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        pl.hops = ev.hops;
        pl.len_bytes = uint16_t(12 + strlen(ev.text));
        strncpy(pl.type_label, "GRP_TXT", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "ch#%u [%uh] %.26s",
                 unsigned(ev.channel_index), unsigned(ev.hops), ev.text);
        pl.valid = true;
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::QueueSizeChanged: {
      _status.queue_size = ev.queue_size;
      ++_version;
      break;
    }
    case MeshEventType::ContactDiscovered: {
      ContactRecord c{};
      memcpy(c.pub_key, ev.pub_key, 32);
      strncpy(c.name, ev.name[0] ? ev.name : "Node", sizeof(c.name) - 1);
      c.role = ev.role;
      c.out_path_len = ev.path_len;
      if (ev.path_len < 64) {
        memcpy(c.out_path, ev.path, ev.path_len);
      }
      c.last_advert_ts = ev.timestamp;
      c.lastmod_ts = ev.timestamp;
      c.gps_lat_e6 = ev.lat_e6;
      c.gps_lon_e6 = ev.lon_e6;
      c.last_snr_q4 = ev.snr_q4;
      c.last_rssi = ev.rssi;
      c.valid = true;
      upsertContact(c);

      if (ev.role == NodeRole::Repeater || ev.role == NodeRole::RoomServer) {
        _repeat_roam.onRepeaterHeard(ev.pub_key, c.name, ev.snr_q4, ev.rssi,
                                     ev.lat_e6, ev.lon_e6, now_ms);
        uint8_t new_pref[32] = {0};
        _repeat_roam.evaluate(now_ms, new_pref);
      }
      _status.rx_packets++;
      _status.last_snr_q4 = ev.snr_q4;
      _status.last_rssi = ev.rssi;
      pushRfHistorySample(ev.rssi, _status.noise_floor_dbm);

      // Store & Forward: queue another transmission of failed/queued DMs once the peer is heard
      if (_ui_prefs.store_and_forward) {
        for (size_t i = 0; i < _msg_count; ++i) {
          size_t phys = (_msg_head + kMaxMessages - _msg_count + i) % kMaxMessages;
          MessageRecord& m = _messages[phys];
          if (m.valid && m.outgoing && !m.is_channel &&
              memcmp(m.peer_prefix, ev.pub_key, 6) == 0 &&
              (m.delivery_state == DeliveryState::Failed || m.delivery_state == DeliveryState::Queued)) {
            m.delivery_state = DeliveryState::Queued;
            m.retry_count = 0;
            m.sent_ms = now_ms;
            queueResend(m.id);
            _ui_prefs.sf_delivered_count++;
          }
        }
      }


      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        pl.hops = (ev.path_len == 0xFF) ? 0 : ev.path_len;
        pl.len_bytes = 104;
        strncpy(pl.type_label, "ADVERT", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "%02X%02X %s [%uh]",
                 ev.pub_key[0], ev.pub_key[1], c.name, unsigned(pl.hops));
        pl.valid = true;
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::AckRecv: {
      if (markAckReceived(ev.ack_crc)) {
        ++_version;
      }
      break;
    }
    case MeshEventType::TraceRecv: {
      _last_trace = ev.trace;
      _last_trace.valid = true;
      snprintf(_status.trace_status, sizeof(_status.trace_status), "Trace #%u back: %u hops",
               unsigned(ev.trace.tag), unsigned(ev.trace.hop_count));
      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        pl.hops = ev.trace.hop_count;
        strncpy(pl.type_label, "TRACE", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "tag %u, %u hops", unsigned(ev.trace.tag),
                 unsigned(ev.trace.hop_count));
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::RemoteResponse: {
      const auto kind = static_cast<RemoteRequestKind>(ev.resp_kind);
      if (ev.path_len != 0xFF) {
        // A flood answer taught MeshCore a path (stored there already); mirror it so the UI shows it
        if (ContactRecord* c = findContactByPubKeyMutable(ev.pub_key)) {
          c->out_path_len = ev.path_len;
          memset(c->out_path, 0, sizeof(c->out_path));
          memcpy(c->out_path, ev.path, c->pathByteLen());
          c->online = true;
        }
      }
      applyRemoteResponse(kind, ev.pub_key, reinterpret_cast<const uint8_t*>(ev.text),
                          ev.data_len <= sizeof(ev.text) ? ev.data_len : sizeof(ev.text));
      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        pl.len_bytes = ev.data_len;
        strncpy(pl.type_label, "RESPONSE", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "%.20s (%u B)", ev.name[0] ? ev.name : "node",
                 unsigned(ev.data_len));
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::NodeDiscovered: {
      DiscoveredNode* slot = nullptr;
      for (size_t i = 0; i < kMaxDiscovered && !slot; ++i) {
        if (_discovered[i].valid && memcmp(_discovered[i].prefix, ev.pub_key, 8) == 0) slot = &_discovered[i];
      }
      for (size_t i = 0; i < kMaxDiscovered && !slot; ++i) {
        if (!_discovered[i].valid) slot = &_discovered[i];
      }
      if (slot) {
        memcpy(slot->prefix, ev.pub_key, 8);
        slot->role = ev.role;
        slot->snr_out_q4 = ev.snr2_q4;
        slot->snr_in_q4 = ev.snr_q4;
        slot->rssi = ev.rssi;
        slot->heard_ms = now_ms;
        slot->valid = true;
      }
      if (!_ui_prefs.sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = now_ms;
        pl.snr_q4 = ev.snr_q4;
        pl.rssi = ev.rssi;
        strncpy(pl.type_label, "DISCOVER", sizeof(pl.type_label) - 1);
        snprintf(pl.summary, sizeof(pl.summary), "%02X%02X%02X%02X out %+ddB", ev.pub_key[0], ev.pub_key[1],
                 ev.pub_key[2], ev.pub_key[3], int(ev.snr2_q4) / 4);
        appendPacketLog(pl);
      }
      ++_version;
      break;
    }
    case MeshEventType::ControlDataRecv:
    case MeshEventType::ChannelDataRecv:
    case MeshEventType::NotifyStateChanged: {
      ++_version;
      break;
    }
    case MeshEventType::None:
    default:
      break;
  }
}

bool AppStore::tickMessageTimeouts(uint32_t now_ms) {
  bool changed = false;
  for (size_t i = 0; i < _msg_count; ++i) {
    size_t phys = (_msg_head + kMaxMessages - _msg_count + i) % kMaxMessages;
    MessageRecord& m = _messages[phys];
    if (!m.valid || !m.outgoing || m.is_channel) continue;
    if (m.delivery_state == DeliveryState::DeliveredAck ||
        m.delivery_state == DeliveryState::Failed) {
      continue;
    }
    if (now_ms < m.sent_ms || (now_ms - m.sent_ms) < kDirectAckTimeoutMs) {
      continue;
    }
    m.sent_ms = now_ms;
    if (m.delivery_state == DeliveryState::SentDirect) {
      m.retry_count++;
      if (m.retry_count >= kMaxDirectRetries) {
        // Path failed: forget it so the next attempt floods
        ContactRecord* peer = findContactByPrefixMutable(m.peer_prefix);
        if (peer) {
          peer->out_path_len = 0xFF;
        }
      }
      queueResend(m.id);
      changed = true;
    } else if (m.delivery_state == DeliveryState::SentFlood) {
      m.retry_count++;
      if (m.retry_count >= kMaxDirectRetries + 1) {
        m.delivery_state = DeliveryState::Failed;
      } else {
        queueResend(m.id);
      }
      changed = true;
    } else if (m.delivery_state == DeliveryState::Queued && m.retry_count > kMaxDirectRetries) {
      m.delivery_state = DeliveryState::Failed;
      changed = true;
    }
  }
  if (changed) ++_version;
  return changed;
}

void AppStore::queueResend(uint32_t msg_id) {
  for (size_t i = 0; i < _resend_count; ++i) {
    if (_resend_ids[i] == msg_id) return;
  }
  if (_resend_count < kMaxResendRequests) _resend_ids[_resend_count++] = msg_id;
}

bool AppStore::takeResendRequest(uint32_t& out_msg_id) {
  if (_resend_count == 0) return false;
  out_msg_id = _resend_ids[0];
  for (size_t i = 1; i < _resend_count; ++i) _resend_ids[i - 1] = _resend_ids[i];
  --_resend_count;
  return true;
}

MessageRecord* AppStore::findMessageByIdMutable(uint32_t msg_id) {
  for (size_t i = 0; i < kMaxMessages; ++i) {
    if (_messages[i].valid && _messages[i].id == msg_id) return &_messages[i];
  }
  return nullptr;
}

size_t AppStore::contactCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].valid) ++n;
  }
  return n;
}

const ContactRecord* AppStore::findContactByPubKey(const uint8_t pub_key[32]) const {
  if (!pub_key) return nullptr;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].valid && memcmp(_contacts[i].pub_key, pub_key, 32) == 0) {
      return &_contacts[i];
    }
  }
  return nullptr;
}

ContactRecord* AppStore::findContactByPubKeyMutable(const uint8_t pub_key[32]) {
  if (!pub_key) return nullptr;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].valid && memcmp(_contacts[i].pub_key, pub_key, 32) == 0) {
      return &_contacts[i];
    }
  }
  return nullptr;
}

const ContactRecord* AppStore::findContactByPrefix(const uint8_t prefix6[6]) const {
  if (!prefix6) return nullptr;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].hasPrefix(prefix6)) return &_contacts[i];
  }
  return nullptr;
}

ContactRecord* AppStore::findContactByPrefixMutable(const uint8_t prefix6[6]) {
  if (!prefix6) return nullptr;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].hasPrefix(prefix6)) return &_contacts[i];
  }
  return nullptr;
}

ContactRecord* AppStore::upsertContact(const ContactRecord& c) {
  ContactRecord* existing = findContactByPubKeyMutable(c.pub_key);
  if (existing) {
    uint16_t unread = existing->unread_count;
    bool fav = existing->favourite;
    bool muted = existing->muted;
    uint8_t tags = (c.tag_mask != 0) ? c.tag_mask : existing->tag_mask;
    char saved_notes[64] = {0};
    strncpy(saved_notes, c.notes[0] ? c.notes : existing->notes, sizeof(saved_notes) - 1);
    uint16_t rbatt = (c.remote_batt_mv != 0) ? c.remote_batt_mv : existing->remote_batt_mv;
    int8_t rtemp = (c.remote_temp_c != 0) ? c.remote_temp_c : existing->remote_temp_c;
    bool ronline = c.online || existing->online;
    int32_t lat = (c.gps_lat_e6 != 0) ? c.gps_lat_e6 : existing->gps_lat_e6;
    int32_t lon = (c.gps_lon_e6 != 0) ? c.gps_lon_e6 : existing->gps_lon_e6;
    *existing = c;
    existing->unread_count = unread;
    existing->favourite = fav;
    existing->muted = muted;
    existing->tag_mask = tags;
    strncpy(existing->notes, saved_notes, sizeof(existing->notes) - 1);
    existing->remote_batt_mv = rbatt;
    existing->remote_temp_c = rtemp;
    existing->online = ronline;
    existing->gps_lat_e6 = lat;
    existing->gps_lon_e6 = lon;
    existing->valid = true;
    ++_version;
    return existing;
  }

  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (!_contacts[i].valid) {
      _contacts[i] = c;
      _contacts[i].valid = true;
      ++_version;
      return &_contacts[i];
    }
  }
  // Overwrite oldest non-favourite contact if full
  size_t oldest_idx = 0;
  uint32_t oldest_ts = UINT32_MAX;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (!_contacts[i].favourite && _contacts[i].last_advert_ts < oldest_ts) {
      oldest_ts = _contacts[i].last_advert_ts;
      oldest_idx = i;
    }
  }
  _contacts[oldest_idx] = c;
  _contacts[oldest_idx].valid = true;
  ++_version;
  return &_contacts[oldest_idx];
}

bool AppStore::removeContact(const uint8_t pub_key[32]) {
  ContactRecord* existing = findContactByPubKeyMutable(pub_key);
  if (!existing) return false;
  *existing = ContactRecord{};
  ++_version;
  return true;
}

void AppStore::clearContactUnread(const uint8_t prefix6[6]) {
  ContactRecord* c = findContactByPrefixMutable(prefix6);
  if (c && c->unread_count > 0) {
    c->unread_count = 0;
    ++_version;
  }
}

uint32_t AppStore::totalUnreadDm() const {
  uint32_t sum = 0;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (_contacts[i].valid && !_contacts[i].muted) sum += _contacts[i].unread_count;
  }
  return sum;
}

size_t AppStore::channelCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid) ++n;
  }
  return n;
}

const ChannelRecord* AppStore::findChannel(uint8_t index) const {
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid && _channels[i].index == index) return &_channels[i];
  }
  return nullptr;
}

ChannelRecord* AppStore::upsertChannel(const ChannelRecord& ch) {
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid && _channels[i].index == ch.index) {
      uint16_t unread = _channels[i].unread_count;
      _channels[i] = ch;
      _channels[i].unread_count = unread;
      _channels[i].valid = true;
      ++_version;
      return &_channels[i];
    }
  }
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (!_channels[i].valid) {
      _channels[i] = ch;
      _channels[i].valid = true;
      ++_version;
      return &_channels[i];
    }
  }
  return nullptr;
}

bool AppStore::removeChannel(uint8_t index) {
  if (index == 0) return false;  // Keep Public channel 0
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid && _channels[i].index == index) {
      _channels[i] = ChannelRecord{};
      ++_version;
      return true;
    }
  }
  return false;
}

void AppStore::clearChannelUnread(uint8_t index) {
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid && _channels[i].index == index) {
      if (_channels[i].unread_count > 0) {
        _channels[i].unread_count = 0;
        ++_version;
      }
      return;
    }
  }
}

uint32_t AppStore::totalUnreadChannels() const {
  uint32_t sum = 0;
  for (size_t i = 0; i < kMaxChannels; ++i) {
    if (_channels[i].valid && !_channels[i].muted) sum += _channels[i].unread_count;
  }
  return sum;
}

const MessageRecord& AppStore::messageAt(size_t logical_idx) const {
  static const MessageRecord kEmpty{};
  if (logical_idx >= _msg_count) return kEmpty;
  size_t phys = (_msg_head + kMaxMessages - _msg_count + logical_idx) % kMaxMessages;
  return _messages[phys];
}

uint32_t AppStore::appendMessage(const MessageRecord& msg) {
  MessageRecord copy = msg;
  copy.id = _next_msg_id++;
  copy.valid = true;
  _messages[_msg_head] = copy;
  _msg_head = (_msg_head + 1) % kMaxMessages;
  if (_msg_count < kMaxMessages) {
    ++_msg_count;
  }
  ++_version;
  return copy.id;
}

bool AppStore::markAckReceived(uint32_t ack_crc) {
  if (ack_crc == 0) return false;
  bool matched = false;
  for (size_t i = 0; i < _msg_count; ++i) {
    size_t phys = (_msg_head + kMaxMessages - _msg_count + i) % kMaxMessages;
    if (_messages[phys].valid && _messages[phys].outgoing &&
        _messages[phys].ack_crc == ack_crc) {
      _messages[phys].delivery_state = DeliveryState::DeliveredAck;
      matched = true;
    }
  }
  if (matched) ++_version;
  return matched;
}


const char* AppStore::cannedAt(size_t idx) const {
  if (idx >= _canned_count) return "";
  return _canned[idx];
}

void AppStore::setCannedMessages(const char* const* list, size_t count) {
  if (!list) return;
  if (count > kMaxCanned) count = kMaxCanned;
  _canned_count = count;
  for (size_t i = 0; i < count; ++i) {
    strncpy(_canned[i], list[i] ? list[i] : "", sizeof(_canned[i]) - 1);
    _canned[i][sizeof(_canned[i]) - 1] = '\0';
  }
}

void AppStore::bytesToHex(const uint8_t* bytes, size_t len, char* out_hex, size_t max_len) {
  if (!out_hex || max_len == 0) return;
  static constexpr char kHex[] = "0123456789abcdef";
  size_t pos = 0;
  for (size_t i = 0; i < len && (pos + 2) < max_len; ++i) {
    out_hex[pos++] = kHex[(bytes[i] >> 4) & 0x0F];
    out_hex[pos++] = kHex[bytes[i] & 0x0F];
  }
  out_hex[pos] = '\0';
}

bool AppStore::hexToBytes(const char* hex, uint8_t* out_bytes, size_t expected_len) {
  if (!hex || !out_bytes) return false;
  for (size_t i = 0; i < expected_len; ++i) {
    int hi = hexNibble(hex[i * 2]);
    if (hi < 0) return false;  // also stops at the terminator before reading past it
    int lo = hexNibble(hex[i * 2 + 1]);
    if (lo < 0) return false;
    out_bytes[i] = uint8_t((hi << 4) | lo);
  }
  return true;
}

bool AppStore::formatContactUri(const ContactRecord& c, char* out_uri, size_t max_len) {
  if (!out_uri || max_len < 120) return false;
  char pub_hex[65] = {0};
  bytesToHex(c.pub_key, 32, pub_hex, sizeof(pub_hex));
  int n = snprintf(out_uri, max_len, "meshcore://contact/add?name=%s&public_key=%s&type=%u",
                   c.name[0] ? c.name : "Node", pub_hex, unsigned(c.role));
  return n > 0 && size_t(n) < max_len;
}

bool AppStore::parseContactUri(const char* uri, ContactRecord& out_c) {
  if (!uri) return false;
  const char* prefix = "meshcore://contact/add?";
  if (strncmp(uri, prefix, strlen(prefix)) != 0) return false;

  out_c = ContactRecord{};
  out_c.role = NodeRole::ChatNode;
  const char* p = uri + strlen(prefix);

  while (*p) {
    const char* amp = strchr(p, '&');
    size_t tok_len = amp ? size_t(amp - p) : strlen(p);
    if (strncmp(p, "name=", 5) == 0) {
      size_t vlen = tok_len - 5;
      if (vlen >= sizeof(out_c.name)) vlen = sizeof(out_c.name) - 1;
      memcpy(out_c.name, p + 5, vlen);
      out_c.name[vlen] = '\0';
    } else if (strncmp(p, "public_key=", 11) == 0) {
      if (tok_len - 11 != 64) return false;
      if (!hexToBytes(p + 11, out_c.pub_key, 32)) return false;
      out_c.valid = true;
    } else if (strncmp(p, "type=", 5) == 0) {
      int t = p[5] - '0';
      if (t >= 1 && t <= 4) out_c.role = static_cast<NodeRole>(t);
    }
    if (!amp) break;
    p = amp + 1;
  }
  return out_c.valid;
}

bool AppStore::formatChannelUri(const ChannelRecord& ch, char* out_uri, size_t max_len) {
  if (!out_uri || max_len < 96) return false;
  char sec_hex[65] = {0};
  uint8_t slen = (ch.secret_len == 32) ? 32 : 16;
  bytesToHex(ch.secret, slen, sec_hex, sizeof(sec_hex));
  int n = snprintf(out_uri, max_len, "meshcore://channel/add?name=%s&secret=%s",
                   ch.name[0] ? ch.name : "Channel", sec_hex);
  return n > 0 && size_t(n) < max_len;
}

bool AppStore::parseChannelUri(const char* uri, ChannelRecord& out_ch) {
  if (!uri) return false;
  const char* prefix = "meshcore://channel/add?";
  if (strncmp(uri, prefix, strlen(prefix)) != 0) return false;

  out_ch = ChannelRecord{};
  const char* p = uri + strlen(prefix);
  while (*p) {
    const char* amp = strchr(p, '&');
    size_t tok_len = amp ? size_t(amp - p) : strlen(p);
    if (strncmp(p, "name=", 5) == 0) {
      size_t vlen = tok_len - 5;
      if (vlen >= sizeof(out_ch.name)) vlen = sizeof(out_ch.name) - 1;
      memcpy(out_ch.name, p + 5, vlen);
      out_ch.name[vlen] = '\0';
    } else if (strncmp(p, "secret=", 7) == 0) {
      size_t hex_len = tok_len - 7;
      if (hex_len == 32) {
        if (!hexToBytes(p + 7, out_ch.secret, 16)) return false;
        out_ch.secret_len = 16;
        out_ch.valid = true;
      } else if (hex_len == 64) {
        if (!hexToBytes(p + 7, out_ch.secret, 32)) return false;
        out_ch.secret_len = 32;
        out_ch.valid = true;
      } else {
        return false;
      }
    }
    if (!amp) break;
    p = amp + 1;
  }
  return out_ch.valid && out_ch.name[0] != '\0';
}

void AppStore::appendPacketLog(const RxPacketLogEntry& entry) {
  RxPacketLogEntry copy = entry;
  copy.valid = true;
  _pkt_logs[_pkt_log_head] = copy;
  _pkt_log_head = (_pkt_log_head + 1) % kMaxPacketLogs;
  if (_pkt_log_count < kMaxPacketLogs) {
    ++_pkt_log_count;
  }
  ++_version;
}

void AppStore::clearPacketLogs() {
  _pkt_log_head = 0;
  _pkt_log_count = 0;
  for (size_t i = 0; i < kMaxPacketLogs; ++i) {
    _pkt_logs[i] = RxPacketLogEntry{};
  }
  ++_version;
}

const RxPacketLogEntry& AppStore::packetLogAt(size_t logical_idx) const {
  static const RxPacketLogEntry kEmpty{};
  if (logical_idx >= _pkt_log_count) return kEmpty;
  size_t phys = (_pkt_log_head + kMaxPacketLogs - _pkt_log_count + logical_idx) % kMaxPacketLogs;
  return _pkt_logs[phys];
}

bool AppStore::exportGpsTrackGpx(const char* filepath, char* out_saved_path, size_t max_len) {
  const char* target = (filepath && filepath[0]) ? filepath : "/tmp/faro_track.gpx";
  FILE* fp = fopen(target, "w");
  if (!fp) return false;

  fprintf(fp,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<gpx version=\"1.1\" creator=\"Faro MeshCore UI\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
          "  <trk>\n"
          "    <name>%s</name>\n"
          "    <trkseg>\n",
          _gps_track.name[0] ? _gps_track.name : "Faro-Alpine-Track");
  for (size_t i = 0; i < _gps_track.point_count && i < 64; ++i) {
    const auto& pt = _gps_track.points[i];
    fprintf(fp,
            "      <trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele></trkpt>\n",
            pt.lat_deg, pt.lon_deg, double(pt.alt_m));
  }
  fprintf(fp,
          "    </trkseg>\n"
          "  </trk>\n"
          "</gpx>\n");
  fclose(fp);
  strncpy(_status.last_gpx_path, target, sizeof(_status.last_gpx_path) - 1);
  if (out_saved_path && max_len > 0) {
    strncpy(out_saved_path, target, max_len - 1);
    out_saved_path[max_len - 1] = '\0';
  }
  ++_version;
  return true;
}

void AppStore::pushRfHistorySample(int16_t rssi_dbm, int16_t noise_dbm) {
  for (size_t i = 0; i < 15; ++i) {
    _status.rssi_history[i] = _status.rssi_history[i + 1];
    _status.noise_history[i] = _status.noise_history[i + 1];
  }
  _status.rssi_history[15] = rssi_dbm;
  _status.noise_history[15] = noise_dbm;
  _status.history_head = uint8_t((_status.history_head + 1) & 0x0F);
}

void AppStore::recordRangeSample(double lat_deg, double lon_deg, int8_t snr_q4, int16_t rssi, uint8_t hops, uint32_t ts) {
  RangeTestSample s{};
  s.lat_deg = lat_deg;
  s.lon_deg = lon_deg;
  s.snr_q4 = snr_q4;
  s.rssi = rssi;
  s.hops = hops;
  s.timestamp = ts;
  s.valid = true;

  if (_range_sample_count < kMaxRangeSamples) {
    _range_samples[_range_sample_count++] = s;
  } else {
    for (size_t i = 0; i + 1 < kMaxRangeSamples; ++i) {
      _range_samples[i] = _range_samples[i + 1];
    }
    _range_samples[kMaxRangeSamples - 1] = s;
  }

  // Also append to CSV on filesystem when available
  const char* csv_path = _status.last_rangetest_csv[0] ? _status.last_rangetest_csv : "/tmp/faro_rangetest.csv";
  bool write_header = false;
  if (FILE* check = fopen(csv_path, "r")) {
    fclose(check);
  } else {
    write_header = true;
  }
  if (FILE* fp = fopen(csv_path, "a")) {
    if (write_header) {
      fprintf(fp, "timestamp,lat_deg,lon_deg,lv95_e,lv95_n,snr_db,rssi_dbm,hops\n");
    }
    int32_t lv_e = 0, lv_n = 0;
    geomath::wgs84ToSwissLV95(lat_deg, lon_deg, lv_e, lv_n);
    fprintf(fp, "%u,%.6f,%.6f,%d,%d,%.2f,%d,%u\n",
            unsigned(ts), lat_deg, lon_deg,
            int(lv_e), int(lv_n),
            double(snr_q4) / 4.0, int(rssi), unsigned(hops));
    fclose(fp);
  }
  ++_version;
}

void AppStore::clearRangeSamples() {
  _range_sample_count = 0;
  for (size_t i = 0; i < kMaxRangeSamples; ++i) {
    _range_samples[i] = RangeTestSample{};
  }
  ++_version;
}

bool AppStore::exportContactsJson(const char* filepath, char* out_saved_path, size_t max_len) {
  const char* target = (filepath && filepath[0]) ? filepath : "/tmp/faro_contacts.json";
  FILE* fp = fopen(target, "w");
  if (!fp) return false;

  char node_name_json[2 * sizeof(_status.node_name)];
  jsonEscape(_status.node_name, node_name_json, sizeof(node_name_json));
  fprintf(fp, "{\n  \"node_name\": \"%s\",\n  \"contacts\": [\n", node_name_json);
  bool first = true;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    const auto& c = _contacts[i];
    if (!c.valid) continue;
    char pub_hex[65] = {0};
    bytesToHex(c.pub_key, 32, pub_hex, sizeof(pub_hex));
    char uri[220] = {0};
    formatContactUri(c, uri, sizeof(uri));
    char name_json[2 * sizeof(c.name)];
    char uri_json[2 * sizeof(uri)];
    jsonEscape(c.name, name_json, sizeof(name_json));
    jsonEscape(uri, uri_json, sizeof(uri_json));
    fprintf(fp,
            "%s    {\"name\": \"%s\", \"pub_key\": \"%s\", \"role\": %u, \"hops\": %u, \"lat\": %.6f, \"lon\": %.6f, \"uri\": \"%s\"}",
            first ? "" : ",\n",
            name_json, pub_hex, unsigned(c.role),
            c.out_path_len == 0xFF ? 0U : unsigned(c.out_path_len),
            c.latDeg(), c.lonDeg(), uri_json);
    first = false;
  }
  fprintf(fp, "\n  ]\n}\n");
  fclose(fp);
  strncpy(_status.last_json_export, target, sizeof(_status.last_json_export) - 1);
  if (out_saved_path && max_len > 0) {
    strncpy(out_saved_path, target, max_len - 1);
    out_saved_path[max_len - 1] = '\0';
  }
  ++_version;
  return true;
}


size_t AppStore::buildTracePath(const uint8_t* out_path, uint8_t out_path_len, const uint8_t* target_pub,
                                bool target_repeats, uint8_t* path, size_t cap, uint8_t& flags) {
  flags = 0;
  if (out_path_len == 0xFF || !path) return 0;  // no known path
  const uint8_t hash_size = uint8_t((out_path_len >> 6) + 1);
  const uint8_t hops = out_path_len & 63;
  // Trace hashes are 1, 2, 4 or 8 bytes (flags bits 0-1 = log2); 2 when the path uses 2-3 byte hashes
  const uint8_t sz = hash_size >= 2 ? 2 : 1;
  flags = sz == 2 ? 1 : 0;
  uint8_t entries[33][2] = {};
  uint8_t n = 0;
  for (uint8_t h = 0; h < hops && n < 32 && out_path; ++h, ++n) memcpy(entries[n], &out_path[h * hash_size], sz);
  if (target_repeats && target_pub && n < 33) memcpy(entries[n++], target_pub, sz);
  if (n == 0) return 0;  // direct neighbour that does not forward
  size_t len = 0;
  for (uint8_t k = 0; k < n && len + sz <= cap; ++k, len += sz) memcpy(&path[len], entries[k], sz);
  for (int k = int(n) - 2; k >= 0 && len + sz <= cap; --k, len += sz) memcpy(&path[len], entries[k], sz);
  return len;
}

bool AppStore::setRegion(RadioRegionId id) {
  if (!RegionPolicy::isKnown(uint8_t(id))) return false;
  if (_region != id) {
    _region = id;
    ++_version;
  }
  return true;
}

size_t AppStore::discoveredCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxDiscovered; ++i) {
    if (_discovered[i].valid) ++n;
  }
  return n;
}

void AppStore::clearDiscovered() {
  for (size_t i = 0; i < kMaxDiscovered; ++i) _discovered[i] = DiscoveredNode{};
  ++_version;
}

namespace {
uint16_t rdU16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t rdU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int32_t rdBe(const uint8_t* p, size_t n, bool is_signed) {
  uint32_t v = 0;
  for (size_t i = 0; i < n; ++i) v = (v << 8) | p[i];
  if (is_signed && n < 4 && (v & (1U << (n * 8 - 1)))) v |= ~((1U << (n * 8)) - 1U);
  return int32_t(v);
}

// Payload size of a Cayenne LPP type, 0 when unknown (parsing stops there)
size_t lppSize(uint8_t type) {
  switch (type) {
    case 0: case 1: case 102: case 104: case 120: case 142: return 1;
    case 2: case 3: case 101: case 103: case 115: case 116: case 117: case 121: case 125: case 128: case 132: return 2;
    case 135: return 3;
    case 100: case 118: case 130: case 131: case 133: return 4;
    case 113: case 134: return 6;
    case 136: return 9;
    default: return 0;
  }
}

const char* contactLabel(const ContactRecord* c, const uint8_t* pub, char* buf, size_t cap) {
  if (c && c->name[0]) return c->name;
  snprintf(buf, cap, "%02X%02X%02X", pub[0], pub[1], pub[2]);
  return buf;
}
}  // namespace

void AppStore::applyRemoteResponse(RemoteRequestKind kind, const uint8_t pub_key[32], const uint8_t* d,
                                   size_t len) {
  ContactRecord* c = pub_key ? findContactByPubKeyMutable(pub_key) : nullptr;
  char idbuf[8] = {0};
  const char* who = contactLabel(c, pub_key ? pub_key : reinterpret_cast<const uint8_t*>("\0\0\0"), idbuf,
                                 sizeof(idbuf));
  char* out = _status.remote_admin_reply;
  const size_t cap = sizeof(_status.remote_admin_reply);
  if (c) c->online = true;

  switch (kind) {
    case RemoteRequestKind::Login: {
      // [RESP_SERVER_LOGIN_OK][keep-alive][is_admin][permissions]... or legacy "OK"
      const bool ok = (len >= 1 && d[0] == 0) || (len >= 2 && d[0] == 'O' && d[1] == 'K');
      const bool admin = ok && len >= 3 && d[0] == 0 && d[2] != 0;
      _status.remote_logged_in = ok;
      snprintf(out, cap, "[LOGIN %s] %.24s%s", ok ? "OK" : "FAILED", who, admin ? " (admin)" : "");
      break;
    }
    case RemoteRequestKind::Status: {
      // Repeater / room server stats (RepeaterStats, little endian)
      if (len < 24) {
        snprintf(out, cap, "[STATUS] %.24s: short reply (%u B)", who, unsigned(len));
        break;
      }
      const uint16_t batt = rdU16(d);
      const int snr = len >= 44 ? int(int16_t(rdU16(d + 42))) / 4 : 0;
      if (c) c->remote_batt_mv = batt;
      snprintf(out, cap,
               "[STATUS] %.24s\nBatt %umV  Queue %u  Noise %ddBm\nRSSI %ddBm SNR %+ddB  RX %u TX %u\n"
               "Airtime %us  Up %uh",
               who, unsigned(batt), unsigned(rdU16(d + 2)), int(int16_t(rdU16(d + 4))),
               int(int16_t(rdU16(d + 6))), snr, unsigned(rdU32(d + 8)), unsigned(rdU32(d + 12)),
               unsigned(rdU32(d + 16)), unsigned(rdU32(d + 20) / 3600U));
      break;
    }
    case RemoteRequestKind::Telemetry:
    case RemoteRequestKind::PathDiscovery: {
      // Cayenne LPP records: [channel][type][big-endian value]
      float volts = -1.0f, temp_c = -999.0f, hum = -1.0f, press = -1.0f;
      size_t i = 0;
      while (i + 2 <= len) {
        const uint8_t ch = d[i];
        const uint8_t type = d[i + 1];
        const size_t n = lppSize(type);
        if (n == 0 || i + 2 + n > len) break;
        const uint8_t* v = d + i + 2;
        if (type == 116 && (ch == 1 || volts < 0.0f)) volts = float(rdBe(v, 2, false)) / 100.0f;
        else if (type == 103 && temp_c <= -999.0f) temp_c = float(rdBe(v, 2, true)) / 10.0f;
        else if (type == 104 && hum < 0.0f) hum = float(v[0]) / 2.0f;
        else if (type == 115 && press < 0.0f) press = float(rdBe(v, 2, false)) / 10.0f;
        else if (type == 136 && c) {
          const int32_t lat = rdBe(v, 3, true);  // 0.0001 deg
          const int32_t lon = rdBe(v + 3, 3, true);
          if (lat != 0 || lon != 0) {
            c->gps_lat_e6 = lat * 100;
            c->gps_lon_e6 = lon * 100;
          }
        }
        i += 2 + n;
      }
      if (c && volts > 0.0f) c->remote_batt_mv = uint16_t(volts * 1000.0f);
      if (c && temp_c > -999.0f) c->remote_temp_c = int8_t(temp_c);
      int pos = snprintf(out, cap, "[%s] %.24s", kind == RemoteRequestKind::PathDiscovery ? "PATH" : "TELEMETRY",
                         who);
      if (kind == RemoteRequestKind::PathDiscovery && c && pos > 0 && size_t(pos) < cap) {
        pos += snprintf(out + pos, cap - size_t(pos), c->hasPath() ? "\nPath found: %u hops" : "\nReplied (flood)",
                        unsigned(c->pathHops()));
      }
      if (volts > 0.0f && pos > 0 && size_t(pos) < cap) pos += snprintf(out + pos, cap - size_t(pos), "\nBatt %.2fV", double(volts));
      if (temp_c > -999.0f && pos > 0 && size_t(pos) < cap) pos += snprintf(out + pos, cap - size_t(pos), "  Temp %.1fC", double(temp_c));
      if (hum >= 0.0f && pos > 0 && size_t(pos) < cap) pos += snprintf(out + pos, cap - size_t(pos), "  Hum %.0f%%", double(hum));
      if (press >= 0.0f && pos > 0 && size_t(pos) < cap) pos += snprintf(out + pos, cap - size_t(pos), "  %.0fhPa", double(press));
      break;
    }
    case RemoteRequestKind::Neighbours: {
      // [total u16][count u16] then [prefix 4][heard secs ago u32][snr x4 i8]
      if (len < 4) {
        snprintf(out, cap, "[NEIGHBOURS] %.24s: short reply", who);
        break;
      }
      const unsigned total = rdU16(d), cnt = rdU16(d + 2);
      int pos = snprintf(out, cap, "[NEIGHBOURS] %.20s: %u of %u", who, cnt, total);
      size_t off = 4;
      for (unsigned k = 0; k < cnt && k < 5 && off + 9 <= len && pos > 0 && size_t(pos) < cap; ++k, off += 9) {
        const ContactRecord* nb = nullptr;
        for (size_t j = 0; j < kMaxContacts && !nb; ++j) {
          if (_contacts[j].valid && memcmp(_contacts[j].pub_key, d + off, 4) == 0) nb = &_contacts[j];
        }
        char nid[12];
        snprintf(nid, sizeof(nid), "%02X%02X%02X%02X", d[off], d[off + 1], d[off + 2], d[off + 3]);
        pos += snprintf(out + pos, cap - size_t(pos), "\n%-12.12s %+ddB %um ago", nb ? nb->name : nid,
                        int(int8_t(d[off + 8])) / 4, unsigned(rdU32(d + off + 4) / 60U));
      }
      break;
    }
    case RemoteRequestKind::Acl: {
      // [prefix 6][permissions] per entry
      int pos = snprintf(out, cap, "[ACL] %.24s: %u entries", who, unsigned(len / 7));
      for (size_t off = 0; off + 7 <= len && off < 7 * 5 && pos > 0 && size_t(pos) < cap; off += 7) {
        const ContactRecord* e = findContactByPrefix(d + off);
        char nid[14];
        snprintf(nid, sizeof(nid), "%02X%02X%02X%02X", d[off], d[off + 1], d[off + 2], d[off + 3]);
        pos += snprintf(out + pos, cap - size_t(pos), "\n%-12.12s perm %u", e ? e->name : nid, unsigned(d[off + 6]));
      }
      break;
    }
    case RemoteRequestKind::MinMaxAvg:
      snprintf(out, cap, "[MIN/MAX/AVG] %.24s: %u B of sensor series", who, unsigned(len));
      break;
    case RemoteRequestKind::None:
    default:
      snprintf(out, cap, "[RESPONSE] %.24s: %u B", who, unsigned(len));
      break;
  }
  ++_version;
}

namespace {
struct PersistedContactMeta {
  uint8_t pub_key[32];
  uint8_t tag_mask;
  bool    favourite;
  bool    muted;
  char    notes[64];
};
constexpr uint32_t kStoreMagic = 0x46415233U;  // "FAR3"
}  // namespace

bool AppStore::saveToFile(const char* filepath) const {
  if (!filepath || !filepath[0]) return false;
  FILE* fp = fopen(filepath, "wb");
  if (!fp) return false;

  uint32_t magic = kStoreMagic;
  fwrite(&magic, sizeof(magic), 1, fp);
  fwrite(&_ui_prefs, sizeof(_ui_prefs), 1, fp);
  fwrite(_regions, sizeof(_regions), 1, fp);
  fwrite(&_gps_track, sizeof(_gps_track), 1, fp);

  uint32_t msg_cnt = uint32_t(_msg_count);
  fwrite(&msg_cnt, sizeof(msg_cnt), 1, fp);
  for (size_t i = 0; i < _msg_count; ++i) {
    const MessageRecord& m = messageAt(i);
    fwrite(&m, sizeof(MessageRecord), 1, fp);
  }

  uint32_t meta_cnt = uint32_t(contactCount());
  fwrite(&meta_cnt, sizeof(meta_cnt), 1, fp);
  for (size_t i = 0; i < kMaxContacts; ++i) {
    if (!_contacts[i].valid) continue;
    PersistedContactMeta pcm{};
    memcpy(pcm.pub_key, _contacts[i].pub_key, 32);
    pcm.tag_mask = _contacts[i].tag_mask;
    pcm.favourite = _contacts[i].favourite;
    pcm.muted = _contacts[i].muted;
    memcpy(pcm.notes, _contacts[i].notes, sizeof(pcm.notes));
    fwrite(&pcm, sizeof(pcm), 1, fp);
  }
  fclose(fp);
  return true;
}

bool AppStore::loadFromFile(const char* filepath) {
  if (!filepath || !filepath[0]) return false;
  FILE* fp = fopen(filepath, "rb");
  if (!fp) return false;

  uint32_t magic = 0;
  if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != kStoreMagic) {
    fclose(fp);
    return false;
  }

  UiPrefs loaded_prefs{};
  if (fread(&loaded_prefs, sizeof(loaded_prefs), 1, fp) == 1) {
    _ui_prefs = loaded_prefs;
    _ui_prefs.search_active = false;
  }
  (void)fread(_regions, sizeof(_regions), 1, fp);
  (void)fread(&_gps_track, sizeof(_gps_track), 1, fp);

  uint32_t msg_cnt = 0;
  if (fread(&msg_cnt, sizeof(msg_cnt), 1, fp) == 1 && msg_cnt <= kMaxMessages) {
    _msg_head = 0;
    _msg_count = 0;
    for (uint32_t i = 0; i < msg_cnt; ++i) {
      MessageRecord m{};
      if (fread(&m, sizeof(m), 1, fp) == 1 && m.valid) {
        appendMessage(m);
      }
    }
  }

  uint32_t meta_cnt = 0;
  if (fread(&meta_cnt, sizeof(meta_cnt), 1, fp) == 1 && meta_cnt <= kMaxContacts) {
    for (uint32_t i = 0; i < meta_cnt; ++i) {
      PersistedContactMeta pcm{};
      if (fread(&pcm, sizeof(pcm), 1, fp) != 1) break;
      if (ContactRecord* c = findContactByPubKeyMutable(pcm.pub_key)) {
        c->tag_mask = pcm.tag_mask;
        c->favourite = pcm.favourite;
        c->muted = pcm.muted;
        memcpy(c->notes, pcm.notes, sizeof(c->notes));
      } else {
        ContactRecord placeholder{};
        memcpy(placeholder.pub_key, pcm.pub_key, 32);
        snprintf(placeholder.name, sizeof(placeholder.name), "Node-%02X%02X", pcm.pub_key[0], pcm.pub_key[1]);
        placeholder.tag_mask = pcm.tag_mask;
        placeholder.favourite = pcm.favourite;
        placeholder.muted = pcm.muted;
        memcpy(placeholder.notes, pcm.notes, sizeof(placeholder.notes));
        placeholder.valid = true;
        upsertContact(placeholder);
      }
    }
  }

  fclose(fp);
  ++_version;
  return true;
}

}  // namespace faro::core
