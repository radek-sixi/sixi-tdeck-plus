// SPDX-License-Identifier: Apache-2.0
// console/CompanionClientMeshActions.cpp — Host companion protocol client (USB Serial & TCP 5000)
#ifndef ARDUINO

#include "CompanionClientMeshActions.h"
#include "core/Sha256.h"
#include "core/Random.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace faro::platform::remote {

using namespace faro::core;

namespace {

uint16_t readU16LE(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t readU32LE(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

int32_t readI32LE(const uint8_t* p) {
  return static_cast<int32_t>(readU32LE(p));
}

void writeU32LE(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v & 0xFF);
  p[1] = uint8_t((v >> 8) & 0xFF);
  p[2] = uint8_t((v >> 16) & 0xFF);
  p[3] = uint8_t((v >> 24) & 0xFF);
}

}  // namespace

CompanionClientMeshActions::CompanionClientMeshActions(AppStore& store, MeshEventBus& bus)
    : _store(store), _bus(bus) {}

CompanionClientMeshActions::~CompanionClientMeshActions() {
  disconnect();
}

void CompanionClientMeshActions::disconnect() {
  if (_fd >= 0) {
    ::close(_fd);
    _fd = -1;
  }
  _rx_buf.clear();
}

bool CompanionClientMeshActions::connectTcp(const char* host, uint16_t port, const uint8_t* token16) {
  disconnect();
  if (!host) return false;

  struct addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", unsigned(port));

  struct addrinfo* res = nullptr;
  if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
    return false;
  }

  int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    return false;
  }

  if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    ::close(fd);
    freeaddrinfo(res);
    return false;
  }
  freeaddrinfo(res);
  _fd = fd;

  (void)token16;
  _store.status().wifi_connected = true;
  strncpy(_store.status().wifi_ip, host, sizeof(_store.status().wifi_ip) - 1);
  strncpy(_last_host, host, sizeof(_last_host) - 1);
  _last_port = port;
  snprintf(_store.status().conn_target, sizeof(_store.status().conn_target), "%s:%u", host, unsigned(port));
  return true;
}

bool CompanionClientMeshActions::connectSerial(const char* dev_path, int baud) {
  disconnect();
  if (!dev_path) return false;
  int fd = ::open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) return false;

  struct termios tty{};
  if (tcgetattr(fd, &tty) == 0) {
    cfmakeraw(&tty);
    speed_t spd = B115200;
    if (baud == 9600) spd = B9600;
    else if (baud == 57600) spd = B57600;
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);
    tty.c_cflag |= (CLOCAL | CREAD);
    tcsetattr(fd, TCSANOW, &tty);
  }
  _fd = fd;
  strncpy(_store.status().conn_target, dev_path, sizeof(_store.status().conn_target) - 1);
  return true;
}

bool CompanionClientMeshActions::sendFrame(const uint8_t* payload, size_t len) {
  if (_fd < 0 || !payload || len == 0 || len > 512) return false;
  std::vector<uint8_t> frame(3 + len);
  frame[0] = '<';
  frame[1] = uint8_t(len & 0xFF);
  frame[2] = uint8_t((len >> 8) & 0xFF);
  memcpy(&frame[3], payload, len);

  size_t total = 0;
  while (total < frame.size()) {
    ssize_t n = ::write(_fd, frame.data() + total, frame.size() - total);
    if (n <= 0) {
      if (errno == EINTR) continue;
      return false;
    }
    total += size_t(n);
  }
  _store.status().tx_packets++;
  return true;
}

bool CompanionClientMeshActions::recvFrameBlocking(std::vector<uint8_t>& out, int timeout_ms) {
  if (_fd < 0) return false;
  struct pollfd pfd{};
  pfd.fd = _fd;
  pfd.events = POLLIN;

  int elapsed = 0;
  while (elapsed < timeout_ms) {
    while (_rx_buf.size() >= 3 && _rx_buf[0] != '>') {
      _rx_buf.erase(_rx_buf.begin());
    }
    if (_rx_buf.size() >= 3) {
      uint16_t flen = readU16LE(&_rx_buf[1]);
      if (_rx_buf.size() >= size_t(3 + flen)) {
        out.assign(_rx_buf.begin() + 3, _rx_buf.begin() + 3 + flen);
        _rx_buf.erase(_rx_buf.begin(), _rx_buf.begin() + 3 + flen);
        _store.status().rx_packets++;
        return true;
      }
    }

    int rc = ::poll(&pfd, 1, 50);
    elapsed += 50;
    if (rc > 0 && (pfd.revents & POLLIN)) {
      uint8_t tmp[256];
      ssize_t n = ::read(_fd, tmp, sizeof(tmp));
      if (n > 0) {
        _rx_buf.insert(_rx_buf.end(), tmp, tmp + n);
      } else if (n == 0) {
        return false;
      }
    }
  }
  return false;
}

void CompanionClientMeshActions::syncStore() {
  if (_fd < 0) return;

  // 1. CMD_DEVICE_QUERY (22)
  uint8_t dq[2] = {CMD_DEVICE_QUERY, 3};
  sendFrame(dq, sizeof(dq));
  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 800) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }

  // 2. CMD_APP_START (1): [1, app_ver(1), reserved(6), app_name...]
  uint8_t app_start[16] = {0};
  app_start[0] = CMD_APP_START;
  app_start[1] = 3;
  memcpy(&app_start[8], "faro-cli", 8);
  sendFrame(app_start, sizeof(app_start));
  if (recvFrameBlocking(resp, 800) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }

  // 3. CMD_GET_CONTACTS (4)
  uint8_t gc[1] = {CMD_GET_CONTACTS};
  sendFrame(gc, sizeof(gc));
  for (int i = 0; i < 64; ++i) {
    if (!recvFrameBlocking(resp, 600) || resp.empty()) break;
    handleIncomingFrame(resp.data(), resp.size());
    if (resp[0] == RESP_CODE_END_OF_CONTACTS) break;
  }

  // 4. CMD_GET_CHANNEL (31) for slots 0..3
  for (uint8_t ch = 0; ch < 4; ++ch) {
    uint8_t gch[2] = {CMD_GET_CHANNEL, ch};
    sendFrame(gch, sizeof(gch));
    if (recvFrameBlocking(resp, 500) && !resp.empty()) {
      handleIncomingFrame(resp.data(), resp.size());
    }
  }

  // 5. CMD_SYNC_NEXT_MESSAGE (10)
  for (int i = 0; i < 16; ++i) {
    uint8_t sm[1] = {CMD_SYNC_NEXT_MESSAGE};
    sendFrame(sm, sizeof(sm));
    if (!recvFrameBlocking(resp, 500) || resp.empty()) break;
    handleIncomingFrame(resp.data(), resp.size());
    if (resp[0] == RESP_CODE_NO_MORE_MESSAGES) break;
  }

  // 6. CMD_GET_BATT_AND_STORAGE (20)
  uint8_t bs[1] = {CMD_GET_BATT_AND_STORAGE};
  sendFrame(bs, sizeof(bs));
  if (recvFrameBlocking(resp, 400) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }
}

void CompanionClientMeshActions::poll() {
  if (_fd < 0) {
    if (_store.status().auto_reconnect && _last_host[0] != '\0') {
      uint32_t now_s = uint32_t(time(nullptr));
      if (now_s - _last_reconnect_ms >= 5) {
        _last_reconnect_ms = now_s;
        if (connectTcp(_last_host, _last_port, nullptr)) {
          syncStore();
        }
      }
    }
    return;
  }
  struct pollfd pfd{_fd, POLLIN, 0};
  bool peer_closed = false;
  while (::poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
    uint8_t tmp[256];
    ssize_t n = ::read(_fd, tmp, sizeof(tmp));
    if (n == 0) {
      peer_closed = true;
      break;
    }
    if (n < 0) break;
    _rx_buf.insert(_rx_buf.end(), tmp, tmp + n);
  }

  while (true) {
    while (_rx_buf.size() >= 3 && _rx_buf[0] != '>') {
      _rx_buf.erase(_rx_buf.begin());
    }
    if (_rx_buf.size() < 3) break;
    uint16_t flen = readU16LE(&_rx_buf[1]);
    if (_rx_buf.size() < size_t(3 + flen)) break;
    std::vector<uint8_t> frame(_rx_buf.begin() + 3, _rx_buf.begin() + 3 + flen);
    _rx_buf.erase(_rx_buf.begin(), _rx_buf.begin() + 3 + flen);
    _store.status().rx_packets++;
    handleIncomingFrame(frame.data(), frame.size());
  }

  if (peer_closed) {
    disconnect();
    _store.status().wifi_connected = false;
    _store.bumpVersion();
  }
}

void CompanionClientMeshActions::handleIncomingFrame(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  uint8_t code = data[0];

  switch (code) {
    case RESP_CODE_SELF_INFO: {
      if (len >= 58) {
        _store.radio().tx_power_dbm = int8_t(data[2]);
        memcpy(_store.status().self_pubkey, &data[4], 32);
        int32_t lat_e6 = readI32LE(&data[36]);
        int32_t lon_e6 = readI32LE(&data[40]);
        _store.gps().lat_deg = double(lat_e6) / 1000000.0;
        _store.gps().lon_deg = double(lon_e6) / 1000000.0;
        _store.gps().valid = (lat_e6 != 0 || lon_e6 != 0);
        uint32_t raw_freq = readU32LE(&data[48]);
        _store.radio().freq_mhz = (raw_freq > 2000000U) ? (float(raw_freq) / 1000000.0f) : (float(raw_freq) / 1000.0f);
        _store.radio().bw_khz = float(readU32LE(&data[52])) / 1000.0f;
        _store.radio().sf = data[56];
        _store.radio().cr = data[57];
        RadioRegionId found = _store.region();
        if (RegionPolicy::findRegionFor(_store.radio().freq_mhz, _store.radio().bw_khz, _store.region(), found)) {
          _store.setRegion(found);
        }
        size_t name_len = len - 58;
        if (name_len >= sizeof(_store.status().node_name)) {
          name_len = sizeof(_store.status().node_name) - 1;
        }
        memcpy(_store.status().node_name, &data[58], name_len);
        _store.status().node_name[name_len] = '\0';
        _store.uiPrefs().first_boot_done = true;
        _store.bumpVersion();
      }
      break;
    }

    case RESP_CODE_CONTACT:
    case PUSH_CODE_NEW_ADVERT:
    case PUSH_CODE_ADVERT: {
      if (len >= 148) {
        ContactRecord c{};
        memcpy(c.pub_key, &data[1], 32);
        c.role = static_cast<NodeRole>(data[33]);
        c.flags = data[34];
        c.out_path_len = data[35];
        memcpy(c.out_path, &data[36], 64);
        memcpy(c.name, &data[100], 31);
        c.name[31] = '\0';
        c.last_advert_ts = readU32LE(&data[132]);
        c.gps_lat_e6 = readI32LE(&data[136]);
        c.gps_lon_e6 = readI32LE(&data[140]);
        c.lastmod_ts = readU32LE(&data[144]);
        c.valid = true;

        bool was_known = (_store.findContactByPubKey(c.pub_key) != nullptr);
        _store.upsertContact(c);

        if (c.role == NodeRole::Repeater) {
          _store.repeatRoam().onRepeaterHeard(c.pub_key, c.name, c.last_snr_q4, c.last_rssi,
                                              c.gps_lat_e6, c.gps_lon_e6, 500);
          uint8_t next_pref[32] = {0};
          _store.repeatRoam().evaluate(500, next_pref);
        }

        if (code == PUSH_CODE_NEW_ADVERT || code == PUSH_CODE_ADVERT) {
          MeshEvent ev{};
          ev.type = MeshEventType::ContactDiscovered;
          memcpy(ev.pub_key, c.pub_key, 32);
          strncpy(ev.name, c.name, sizeof(ev.name) - 1);
          ev.role = c.role;
          ev.lat_e6 = c.gps_lat_e6;
          ev.lon_e6 = c.gps_lon_e6;
          ev.path_len = c.out_path_len;
          ev.timestamp = c.last_advert_ts;
          ev.is_new_contact = !was_known;
          _bus.push(ev);
        }
      }
      break;
    }

    case RESP_CODE_CHANNEL_INFO: {
      if (len >= 50) {
        ChannelRecord ch{};
        ch.index = data[1];
        memcpy(ch.name, &data[2], 31);
        ch.name[31] = '\0';
        memcpy(ch.secret, &data[34], 16);
        ch.secret_len = 16;
        ch.valid = ch.name[0] != '\0';
        if (ch.valid) {
          _store.upsertChannel(ch);
        }
      }
      break;
    }

    case RESP_CODE_CONTACT_MSG_RECV_V3: {
      if (len >= 16) {
        MeshEvent ev{};
        ev.type = MeshEventType::DirectMessageRecv;
        ev.snr_q4 = int8_t(data[1]);
        ev.rssi = -85;
        memcpy(ev.pub_key, &data[4], 6);
        const ContactRecord* c = _store.findContactByPrefix(&data[4]);
        if (c) {
          memcpy(ev.pub_key, c->pub_key, 32);
          strncpy(ev.name, c->name, sizeof(ev.name) - 1);
        }
        ev.hops = data[10];
        ev.txt_type = data[11];
        ev.timestamp = readU32LE(&data[12]);
        size_t tlen = len - 16;
        if (tlen >= sizeof(ev.text)) tlen = sizeof(ev.text) - 1;
        memcpy(ev.text, &data[16], tlen);
        ev.text[tlen] = '\0';
        if (ev.txt_type == 1) {
          snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
                   "[REMOTE CLI REPLY]\n%s", ev.text);
        }
        _bus.push(ev);
      }
      break;
    }

    case RESP_CODE_CHANNEL_MSG_RECV_V3: {
      if (len >= 11) {
        MeshEvent ev{};
        ev.type = MeshEventType::ChannelMessageRecv;
        ev.snr_q4 = int8_t(data[1]);
        ev.rssi = -85;
        ev.channel_index = data[4];
        ev.hops = data[5];
        ev.txt_type = data[6];
        ev.timestamp = readU32LE(&data[7]);
        size_t tlen = len - 11;
        if (tlen >= sizeof(ev.text)) tlen = sizeof(ev.text) - 1;
        memcpy(ev.text, &data[11], tlen);
        ev.text[tlen] = '\0';
        _bus.push(ev);
      }
      break;
    }

    case RESP_CODE_SENT: {
      if (len >= 10) {
        _last_sent_crc = readU32LE(&data[2]);
      }
      break;
    }

    case PUSH_CODE_SEND_CONFIRMED: {
      if (len >= 9) {
        MeshEvent ev{};
        ev.type = MeshEventType::AckRecv;
        ev.ack_crc = readU32LE(&data[1]);
        _bus.push(ev);
      }
      break;
    }

    case PUSH_CODE_MSG_WAITING: {
      uint8_t sm[1] = {CMD_SYNC_NEXT_MESSAGE};
      sendFrame(sm, sizeof(sm));
      break;
    }

    case RESP_CODE_BATT_AND_STORAGE: {
      if (len >= 3) {
        uint16_t mv = readU16LE(&data[1]);
        if (mv >= 2500 && mv <= 4500) {
          _store.status().batt_mv = mv;
          int pct = (int(mv) - 3300) * 100 / 900;
          if (pct < 0) pct = 0;
          if (pct > 100) pct = 100;
          _store.status().batt_pct = uint8_t(pct);
          _store.bumpVersion();
        }
      }
      break;
    }

    case RESP_CODE_CLI_REPLY: {
      if (len >= 2) {
        size_t rlen = len - 1;
        if (rlen >= sizeof(_store.status().last_cli_reply)) {
          rlen = sizeof(_store.status().last_cli_reply) - 1;
        }
        memcpy(_store.status().last_cli_reply, &data[1], rlen);
        _store.status().last_cli_reply[rlen] = '\0';
        _store.bumpVersion();
      }
      break;
    }

    case PUSH_CODE_LOGIN_SUCCESS: {
      _store.status().remote_logged_in = true;
      snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
               "[LOGIN OK] Session established with remote node.");
      _store.bumpVersion();
      break;
    }

    case PUSH_CODE_STATUS_RESPONSE: {
      if (len >= 12) {
        uint16_t bmv = readU16LE(&data[7]);
        snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
                 "[STATUS] Batt: %umV | Queue: %u | RSSI: %ddBm | SNR: %+ddB",
                 unsigned(bmv), unsigned(readU16LE(&data[9])),
                 int(int16_t(readU16LE(&data[11]))),
                 len >= 15 ? int(int8_t(data[14])) / 4 : 0);
      } else {
        snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
                 "[STATUS] Received %zu B response from repeater/room.", len);
      }
      _store.bumpVersion();
      break;
    }

    case PUSH_CODE_TELEMETRY_RESPONSE: {
      ContactRecord* c = (len >= 7) ? _store.findContactByPrefixMutable(&data[1]) : nullptr;
      if (c) {
        c->online = true;
      }
      snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
               "[TELEMETRY] Node is ONLINE (%zu B CayenneLPP telemetry received).", len);
      _store.bumpVersion();
      break;
    }

    case PUSH_CODE_BINARY_RESPONSE: {
      snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
               "[BINARY RESP] Received %zu B (Neighbors / ACL / MMA data).", len);
      _store.bumpVersion();
      break;
    }

    case RESP_CODE_CUSTOM_VARS: {
      if (len >= 2) {
        size_t vlen = len - 1;
        if (vlen >= sizeof(_store.status().custom_vars_str)) {
          vlen = sizeof(_store.status().custom_vars_str) - 1;
        }
        memcpy(_store.status().custom_vars_str, &data[1], vlen);
        _store.status().custom_vars_str[vlen] = '\0';
        _store.bumpVersion();
      }
      break;
    }

    case PUSH_CODE_LOG_RX_DATA:
    case PUSH_CODE_RAW_DATA: {
      if (len >= 4 && !_store.uiPrefs().sniffer_paused) {
        RxPacketLogEntry pl{};
        pl.timestamp_ms = uint32_t(time(nullptr));
        pl.snr_q4 = int8_t(data[1]);
        pl.rssi = int16_t(int8_t(data[2]));
        pl.len_bytes = uint16_t(len - 3);
        uint8_t hdr = data[3];
        uint8_t ptype = (hdr >> 2) & 0x0F;
        static const char* const kPktTypes[16] = {
          "REQ", "RESP", "TXT_MSG", "ACK", "ADVERT", "GRP_TXT", "GRP_DAT", "ANON",
          "PATH", "TRACE", "MULTIP", "CTRL", "RAW_C", "RAW_D", "RAW_E", "RAW_F"
        };
        strncpy(pl.type_label, (code == PUSH_CODE_RAW_DATA) ? "RAW_DATA" : kPktTypes[ptype],
                sizeof(pl.type_label) - 1);
        pl.hops = (len >= 5) ? (data[4] & 0x3F) : 0;
        char hex_preview[20] = {0};
        size_t hlen = (len > 3) ? ((len - 3 > 6) ? 6 : (len - 3)) : 0;
        AppStore::bytesToHex(&data[3], hlen, hex_preview, sizeof(hex_preview));
        snprintf(pl.summary, sizeof(pl.summary), "len=%uB [%uh] %s..",
                 unsigned(pl.len_bytes), unsigned(pl.hops), hex_preview);
        pl.valid = true;
        _store.appendPacketLog(pl);
      }
      break;
    }

    case PUSH_CODE_TRACE_DATA: {
      if (len >= 12) {
        uint8_t plen = data[2];
        if (plen > 16) plen = 16;
        if (len >= size_t(12 + plen * 2)) {
          MeshEvent ev{};
          ev.type = MeshEventType::TraceRecv;
          ev.trace.hop_count = plen;
          ev.trace.flags = data[3];
          ev.trace.tag = readU32LE(&data[4]);
          ev.trace.auth_code = readU32LE(&data[8]);
          for (uint8_t i = 0; i < plen; ++i) {
            ev.trace.hops[i].hash = data[12 + i];
            ev.trace.hops[i].snr_q4 = int8_t(data[12 + plen + i]);
          }
          ev.trace.valid = true;
          _bus.push(ev);
        }
      }
      break;
    }

    default:
      break;
  }
}

bool CompanionClientMeshActions::resendDirectMessage(uint32_t msg_id) {
  MessageRecord* m = _store.findMessageByIdMutable(msg_id);
  if (!m || !m->outgoing || m->is_channel) return false;
  size_t tlen = strlen(m->text);
  std::vector<uint8_t> pkt(13 + tlen);
  pkt[0] = CMD_SEND_TXT_MSG;
  pkt[1] = 0;  // TXT_TYPE_PLAIN
  pkt[2] = uint8_t(m->retry_count & 3);  // attempt
  writeU32LE(&pkt[3], uint32_t(time(nullptr)));
  memcpy(&pkt[7], m->peer_prefix, 6);
  memcpy(&pkt[13], m->text, tlen);
  if (!sendFrame(pkt.data(), pkt.size())) {
    m->delivery_state = DeliveryState::Failed;
    _store.bumpVersion();
    return false;
  }
  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 300) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }
  m = _store.findMessageByIdMutable(msg_id);  // handleIncomingFrame may have appended messages
  if (m) {
    m->ack_crc = _last_sent_crc;
    m->delivery_state = DeliveryState::SentDirect;
  }
  _store.bumpVersion();
  return true;
}

bool CompanionClientMeshActions::sendDirectMessage(const uint8_t peer_pubkey[32], const char* text,
                                                   uint32_t& out_ack_crc, DeliveryState& out_state) {
  if (!peer_pubkey || !text) return false;
  size_t tlen = strlen(text);
  std::vector<uint8_t> pkt(13 + tlen);
  pkt[0] = CMD_SEND_TXT_MSG;
  pkt[1] = 0;  // TXT_TYPE_PLAIN
  pkt[2] = 0;  // attempt 0
  writeU32LE(&pkt[3], uint32_t(time(nullptr)));
  memcpy(&pkt[7], peer_pubkey, 6);
  memcpy(&pkt[13], text, tlen);
  if (!sendFrame(pkt.data(), pkt.size())) return false;

  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 300) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }
  out_ack_crc = _last_sent_crc;
  out_state = DeliveryState::SentDirect;
  return true;
}

bool CompanionClientMeshActions::sendChannelMessage(uint8_t channel_index, const char* text) {
  if (!text) return false;
  size_t tlen = strlen(text);
  std::vector<uint8_t> pkt(7 + tlen);
  pkt[0] = CMD_SEND_CHANNEL_TXT_MSG;
  pkt[1] = 0;  // TXT_TYPE_PLAIN
  pkt[2] = channel_index;
  writeU32LE(&pkt[3], uint32_t(time(nullptr)));
  memcpy(&pkt[7], text, tlen);
  return sendFrame(pkt.data(), pkt.size());
}

bool CompanionClientMeshActions::sendZeroHopAdvert() {
  uint8_t pkt[2] = {CMD_SEND_SELF_ADVERT, 0};
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::sendFloodAdvert() {
  uint8_t pkt[2] = {CMD_SEND_SELF_ADVERT, 1};
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::addOrUpdateContact(const ContactRecord& contact) {
  _store.upsertContact(contact);
  return true;
}

bool CompanionClientMeshActions::removeContact(const uint8_t pub_key[32]) {
  _store.removeContact(pub_key);
  uint8_t pkt[33];
  pkt[0] = CMD_REMOVE_CONTACT;
  memcpy(&pkt[1], pub_key, 32);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::resetContactPath(const uint8_t pub_key[32]) {
  uint8_t pkt[33];
  pkt[0] = CMD_RESET_PATH;
  memcpy(&pkt[1], pub_key, 32);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setPreferredRepeater(const uint8_t repeater_pubkey[32]) {
  if (!repeater_pubkey) return false;
  _store.repeatRoam().onRepeaterHeard(repeater_pubkey, "Preferred", 40, -70, 0, 0, 100);
  uint8_t next[32] = {0};
  _store.repeatRoam().evaluate(100, next);
  return true;
}

bool CompanionClientMeshActions::addChannel(const char* name, const uint8_t* secret, uint8_t secret_len) {
  if (!name || !secret) return false;
  uint8_t idx = uint8_t(_store.channelCount());
  uint8_t pkt[50] = {0};
  pkt[0] = CMD_SET_CHANNEL;
  pkt[1] = idx;
  strncpy(reinterpret_cast<char*>(&pkt[2]), name, 31);
  memcpy(&pkt[34], secret, secret_len <= 16 ? secret_len : 16);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::addHashtagChannel(const char* hashtag_name) {
  if (!hashtag_name) return false;
  // MeshCore hashtag channels: key = first 16 bytes of SHA-256("#name"), as the companion app derives it
  uint8_t digest[32] = {0};
  sha256(reinterpret_cast<const uint8_t*>(hashtag_name), strlen(hashtag_name), digest);
  return addChannel(hashtag_name, digest, 16);
}

bool CompanionClientMeshActions::removeChannel(uint8_t channel_index) {
  _store.removeChannel(channel_index);
  uint8_t pkt[50] = {0};
  pkt[0] = CMD_SET_CHANNEL;
  pkt[1] = channel_index;
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::sendTracePath(const uint8_t target_pubkey[32], uint32_t& out_tag) {
  if (!target_pubkey) return false;
  out_tag = ++_trace_seq;
  uint8_t pkt[11];
  pkt[0] = CMD_SEND_TRACE_PATH;
  writeU32LE(&pkt[1], out_tag);
  writeU32LE(&pkt[5], 0);
  pkt[9] = 0;
  pkt[10] = target_pubkey[0];
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::requestTelemetry(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[36] = {0};
  pkt[0] = CMD_SEND_TELEMETRY_REQ;
  memcpy(&pkt[4], target_pubkey, 32);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setRadioConfig(const RadioConfig& in) {
  const RadioConfig cfg = RadioConfig::sanitized(in, _store.regionInfo());
  if (!RadioConfig::isValid(cfg, _store.regionInfo())) return false;
  _store.radio() = cfg;
  uint8_t pkt[12];
  pkt[0] = CMD_SET_RADIO_PARAMS;
  writeU32LE(&pkt[1], uint32_t(cfg.freq_mhz * 1000.0f));
  writeU32LE(&pkt[5], uint32_t(cfg.bw_khz * 1000.0f));
  pkt[9] = cfg.sf;
  pkt[10] = cfg.cr;
  pkt[11] = cfg.client_repeat ? 1 : 0;
  sendFrame(pkt, sizeof(pkt));

  uint8_t pwr[2] = {CMD_SET_RADIO_TX_POWER, uint8_t(cfg.tx_power_dbm)};
  if (!sendFrame(pwr, sizeof(pwr))) return false;
  // Keep the node's duty-cycle limit (airtime factor) inside the band rules as well
  return setTuningParams(_store.status().rx_delay, _store.status().interference_db,
                         _store.status().agc_reset_sec, _store.status().double_acks);
}

bool CompanionClientMeshActions::setNodeName(const char* name) {
  if (!name) return false;
  strncpy(_store.status().node_name, name, sizeof(_store.status().node_name) - 1);
  size_t nlen = strlen(_store.status().node_name);
  std::vector<uint8_t> pkt(1 + nlen);
  pkt[0] = CMD_SET_ADVERT_NAME;
  memcpy(&pkt[1], _store.status().node_name, nlen);
  return sendFrame(pkt.data(), pkt.size());
}

bool CompanionClientMeshActions::setWifiConfig(bool enabled, const char* ssid, const char* pwd) {
  _store.status().wifi_enabled = enabled;
  _store.status().wifi_connected = enabled;
  if (ssid && ssid[0]) {
    strncpy(_store.status().wifi_ssid, ssid, sizeof(_store.status().wifi_ssid) - 1);
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "set wifi.ssid %s", ssid);
    runCliCommand(cmd, nullptr, 0);
  }
  if (pwd && pwd[0]) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "set wifi.pwd %s", pwd);
    runCliCommand(cmd, nullptr, 0);
  }
  runCliCommand(enabled ? "set wifi.enabled 1" : "set wifi.enabled 0", nullptr, 0);
  _store.bumpVersion();
  return true;
}

bool CompanionClientMeshActions::setBlePin(uint32_t pin) {
  _store.status().ble_pin = pin;
  uint8_t pkt[5];
  pkt[0] = CMD_SET_DEVICE_PIN;
  writeU32LE(&pkt[1], pin);
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setRepeatEnabled(bool enabled) {
  if (enabled && !RadioConfig::isAllowedRepeatFrequency(_store.radio().freq_mhz, _store.regionInfo())) {
    return false;
  }
  RadioConfig cfg = _store.radio();
  cfg.client_repeat = enabled;
  return setRadioConfig(cfg);
}

bool CompanionClientMeshActions::setRegion(RadioRegionId id) {
  const RadioRegionId before = _store.region();
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

bool CompanionClientMeshActions::setDefaultScope(const char* scope_name) {
  if (!scope_name) return false;
  strncpy(_store.status().active_scope_name, scope_name, sizeof(_store.status().active_scope_name) - 1);
  return true;
}

bool CompanionClientMeshActions::setGpsConfig(bool enabled, uint32_t, uint32_t, bool) {
  _store.gps().valid = enabled;
  return true;
}


bool CompanionClientMeshActions::exportIdentityHex(char* out_hex, size_t max_len) {
  if (!out_hex || max_len < 65) return false;
  AppStore::bytesToHex(_store.status().self_pubkey, 32, out_hex, max_len);
  return true;
}

bool CompanionClientMeshActions::importIdentityHex(const char*) { return true; }
bool CompanionClientMeshActions::factoryReset() {
  uint8_t pkt[9] = {CMD_FACTORY_RESET, 'r', 'e', 's', 'e', 't', 0, 0, 0};
  return sendFrame(pkt, sizeof(pkt));
}
void CompanionClientMeshActions::rebootToDfu() {
  rebootRadio();
}

bool CompanionClientMeshActions::runCliCommand(const char* cmd, char* out_reply, size_t max_len) {
  if (!cmd || !cmd[0]) return false;
  size_t clen = strlen(cmd);
  std::vector<uint8_t> pkt(1 + clen);
  pkt[0] = CMD_RUN_CLI_COMMAND;
  memcpy(&pkt[1], cmd, clen);
  if (!sendFrame(pkt.data(), pkt.size())) return false;

  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 600) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }
  if (out_reply && max_len > 0) {
    strncpy(out_reply, _store.status().last_cli_reply, max_len - 1);
    out_reply[max_len - 1] = '\0';
  }
  _store.bumpVersion();
  return true;
}

bool CompanionClientMeshActions::sendRemoteLogin(const uint8_t target_pubkey[32], const char* password) {
  if (!target_pubkey || !password) return false;
  size_t plen = strlen(password);
  std::vector<uint8_t> pkt(33 + plen);
  pkt[0] = CMD_SEND_LOGIN;
  memcpy(&pkt[1], target_pubkey, 32);
  memcpy(&pkt[33], password, plen);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGIN] Sent login request (pwd len=%zu)...", plen);
  _store.bumpVersion();
  return sendFrame(pkt.data(), pkt.size());
}

bool CompanionClientMeshActions::sendRemoteLogout(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[33];
  pkt[0] = CMD_LOGOUT;
  memcpy(&pkt[1], target_pubkey, 32);
  _store.status().remote_logged_in = false;
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[LOGOUT] Disconnected admin session.");
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::requestRemoteStatus(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[33];
  pkt[0] = CMD_SEND_STATUS_REQ;
  memcpy(&pkt[1], target_pubkey, 32);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[STATUS REQ] Requesting repeater/room status...");
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::requestRemoteBinary(const uint8_t target_pubkey[32], const char* subcmd) {
  if (!target_pubkey || !subcmd) return false;
  uint8_t pkt[36] = {0};
  pkt[0] = CMD_SEND_BINARY_REQ;
  memcpy(&pkt[1], target_pubkey, 32);
  pkt[33] = (strncmp(subcmd, "acl", 3) == 0) ? 2 : ((strncmp(subcmd, "mma", 3) == 0) ? 3 : 1);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[BINARY REQ] Sent '%s' query to remote node...", subcmd);
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::sendRemoteCli(const uint8_t target_pubkey[32], const char* cli_cmd) {
  if (!target_pubkey || !cli_cmd) return false;
  size_t clen = strlen(cli_cmd);
  std::vector<uint8_t> pkt(13 + clen);
  pkt[0] = CMD_SEND_TXT_MSG;
  pkt[1] = 1;  // TXT_TYPE_CLI_DATA
  pkt[2] = 0;
  writeU32LE(&pkt[3], uint32_t(time(nullptr)));
  memcpy(&pkt[7], target_pubkey, 6);
  memcpy(&pkt[13], cli_cmd, clen);
  snprintf(_store.status().remote_admin_reply, sizeof(_store.status().remote_admin_reply),
           "[REMOTE CLI] > %s (waiting for RF reply...)", cli_cmd);
  _store.bumpVersion();
  return sendFrame(pkt.data(), pkt.size());
}

bool CompanionClientMeshActions::shareContactZeroHop(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[33];
  pkt[0] = CMD_SHARE_CONTACT;
  memcpy(&pkt[1], target_pubkey, 32);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::findPath(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[34] = {0};
  pkt[0] = CMD_SEND_PATH_DISCOVERY_REQ;
  pkt[1] = 0;
  memcpy(&pkt[2], target_pubkey, 32);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::scanNearbyNodes() {
  uint8_t pkt[2] = {CMD_SEND_CONTROL_DATA, 0x80};  // DISCOVER_REQ
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::syncDeviceTime(uint32_t unix_ts) {
  if (unix_ts == 0) unix_ts = uint32_t(time(nullptr));
  uint8_t pkt[5];
  pkt[0] = CMD_SET_DEVICE_TIME;
  writeU32LE(&pkt[1], unix_ts);
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setTuningParams(float rx_delay, uint8_t interference_db,
                                                 uint8_t agc_sec, bool double_ack) {
  _store.status().rx_delay = rx_delay;
  _store.status().interference_db = interference_db;
  _store.status().agc_reset_sec = agc_sec;
  _store.status().double_acks = double_ack;
  uint8_t pkt[9] = {0};
  pkt[0] = CMD_SET_TUNING_PARAMS;
  writeU32LE(&pkt[1], uint32_t(rx_delay * 1000.0f));
  float af = _store.radio().airtime_factor;
  if (!(af >= _store.regionInfo().min_airtime_factor)) af = _store.regionInfo().min_airtime_factor;  // region duty
  writeU32LE(&pkt[5], uint32_t(af * 1000.0f));
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setTelemetryPerms(uint8_t telemetry_perm, uint8_t loc_policy) {
  _store.status().telemetry_perm = telemetry_perm;
  _store.status().loc_policy = loc_policy;
  uint8_t pkt[4] = {CMD_SET_OTHER_PARAMS, uint8_t(_store.status().auto_add_contacts ? 0 : 1),
                    telemetry_perm, loc_policy};
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setAutoAddConfig(bool enabled, uint8_t max_hops) {
  _store.status().auto_add_contacts = enabled;
  _store.status().auto_add_max_hops = max_hops;
  uint8_t pkt[3] = {CMD_SET_AUTOADD_CONFIG, uint8_t(enabled ? 0x0F : 0x00), max_hops};
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::setPathHashMode(uint8_t hash_bytes) {
  if (hash_bytes < 1) hash_bytes = 1;
  if (hash_bytes > 3) hash_bytes = 3;
  _store.status().path_hash_bytes = hash_bytes;
  _store.radio().path_hash_mode = uint8_t(hash_bytes - 1);
  uint8_t pkt[3] = {CMD_SET_PATH_HASH_MODE, 0, uint8_t(hash_bytes - 1)};
  _store.bumpVersion();
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::rebootRadio() {
  uint8_t pkt[1] = {CMD_REBOOT};
  return sendFrame(pkt, sizeof(pkt));
}

bool CompanionClientMeshActions::useLastAdvertPath(const uint8_t target_pubkey[32]) {
  if (!target_pubkey) return false;
  uint8_t pkt[34] = {0};
  pkt[0] = CMD_GET_ADVERT_PATH;
  pkt[1] = 0;
  memcpy(&pkt[2], target_pubkey, 32);
  if (!sendFrame(pkt, sizeof(pkt))) return false;

  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 400) && resp.size() >= 6 && resp[0] == RESP_CODE_ADVERT_PATH) {
    uint8_t plen = resp[5];
    size_t path_bytes = resp.size() - 6;
    if (path_bytes > 64) path_bytes = 64;
    if (ContactRecord* c = _store.findContactByPubKeyMutable(target_pubkey)) {
      c->out_path_len = plen & 0x3F;
      memcpy(c->out_path, &resp[6], path_bytes);
      addOrUpdateContact(*c);
      return true;
    }
  }
  return false;
}

bool CompanionClientMeshActions::exportContactUri(const uint8_t target_pubkey[32],
                                                  char* out_uri, size_t max_len) {
  if (!target_pubkey || !out_uri || max_len == 0) return false;
  const ContactRecord* c = _store.findContactByPubKey(target_pubkey);
  if (!c) return false;
  if (!AppStore::formatContactUri(*c, out_uri, max_len)) return false;
  strncpy(_store.uiPrefs().clipboard, out_uri, sizeof(_store.uiPrefs().clipboard) - 1);
  _store.bumpVersion();
  return true;
}

bool CompanionClientMeshActions::importContactUri(const char* uri_or_hex) {
  if (!uri_or_hex || !uri_or_hex[0]) return false;
  ContactRecord c{};
  if (AppStore::parseContactUri(uri_or_hex, c)) {
    return addOrUpdateContact(c);
  }
  return false;
}

bool CompanionClientMeshActions::getCustomVars(char* out_vars, size_t max_len) {
  uint8_t pkt[1] = {CMD_GET_CUSTOM_VARS};
  if (!sendFrame(pkt, sizeof(pkt))) return false;
  std::vector<uint8_t> resp;
  if (recvFrameBlocking(resp, 400) && !resp.empty()) {
    handleIncomingFrame(resp.data(), resp.size());
  }
  if (out_vars && max_len > 0) {
    strncpy(out_vars, _store.status().custom_vars_str, max_len - 1);
    out_vars[max_len - 1] = '\0';
  }
  return true;
}

bool CompanionClientMeshActions::setCustomVar(const char* key_val) {
  if (!key_val || !strchr(key_val, ':')) return false;
  size_t kvlen = strlen(key_val);
  std::vector<uint8_t> pkt(1 + kvlen);
  pkt[0] = CMD_SET_CUSTOM_VAR;
  memcpy(&pkt[1], key_val, kvlen);
  if (!sendFrame(pkt.data(), pkt.size())) return false;
  std::vector<uint8_t> resp;
  (void)recvFrameBlocking(resp, 300);
  char dummy[128];
  getCustomVars(dummy, sizeof(dummy));
  return true;
}

bool CompanionClientMeshActions::reconnectTarget(const char* target_spec) {
  if (!target_spec || !target_spec[0]) return false;
  if (target_spec[0] == '/') {
    if (connectSerial(target_spec, 115200)) {
      syncStore();
      return true;
    }
    return false;
  }
  char host[64] = {0};
  uint16_t port = 5000;
  const char* colon = strchr(target_spec, ':');
  if (colon) {
    size_t hlen = size_t(colon - target_spec);
    if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
    memcpy(host, target_spec, hlen);
    port = uint16_t(atoi(colon + 1));
  } else {
    strncpy(host, target_spec, sizeof(host) - 1);
  }
  if (connectTcp(host, port, nullptr)) {
    syncStore();
    return true;
  }
  return false;
}

bool CompanionClientMeshActions::scanLanRadios(char* out_found, size_t max_len) {
  if (out_found && max_len > 0) out_found[0] = '\0';
  // First check known/current host or ARP table entries on port 5000
  FILE* arp = fopen("/proc/net/arp", "r");
  if (!arp) return false;
  char line[256];
  // Skip header
  (void)fgets(line, sizeof(line), arp);
  while (fgets(line, sizeof(line), arp)) {
    char ip[64] = {0};
    if (sscanf(line, "%63s", ip) == 1 && ip[0] >= '1' && ip[0] <= '9') {
      int sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
      if (sock < 0) continue;
      struct sockaddr_in sa{};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(5000);
      if (inet_pton(AF_INET, ip, &sa.sin_addr) == 1) {
        (void)::connect(sock, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
        struct pollfd pfd{sock, POLLOUT, 0};
        if (::poll(&pfd, 1, 120) > 0 && (pfd.revents & POLLOUT)) {
          int err = 0;
          socklen_t elen = sizeof(err);
          if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) {
            ::close(sock);
            fclose(arp);
            snprintf(_store.status().conn_target, sizeof(_store.status().conn_target), "%s:5000", ip);
            if (out_found && max_len > 0) {
              snprintf(out_found, max_len, "%s:5000", ip);
            }
            _store.bumpVersion();
            return true;
          }
        }
      }
      ::close(sock);
    }
  }
  fclose(arp);
  return false;
}

}  // namespace faro::platform::remote

#endif  // !ARDUINO
