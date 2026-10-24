// SPDX-License-Identifier: Apache-2.0
// tests/unit/test_companion_client.cpp — End-to-end tests for CompanionClientMeshActions over local TCP
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "core/AppStore.h"
#include "core/EventBus.h"
#include "CompanionClientMeshActions.h"

using namespace faro::core;
using namespace faro::platform::remote;

namespace {

void sendRadioFrame(int fd, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> frame(3 + payload.size());
  frame[0] = '>';
  frame[1] = uint8_t(payload.size() & 0xFF);
  frame[2] = uint8_t((payload.size() >> 8) & 0xFF);
  memcpy(&frame[3], payload.data(), payload.size());
  (void)::write(fd, frame.data(), frame.size());
}

bool recvClientFrame(int fd, std::vector<uint8_t>& out) {
  uint8_t hdr[3] = {0};
  size_t got = 0;
  while (got < 3) {
    ssize_t n = ::read(fd, hdr + got, 3 - got);
    if (n <= 0) return false;
    got += size_t(n);
  }
  if (hdr[0] != '<') return false;
  uint16_t len = uint16_t(hdr[1]) | (uint16_t(hdr[2]) << 8);
  out.resize(len);
  got = 0;
  while (got < len) {
    ssize_t n = ::read(fd, out.data() + got, len - got);
    if (n <= 0) return false;
    got += size_t(n);
  }
  return true;
}

void writeU32LE(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v & 0xFF);
  p[1] = uint8_t((v >> 8) & 0xFF);
  p[2] = uint8_t((v >> 16) & 0xFF);
  p[3] = uint8_t((v >> 24) & 0xFF);
}

void test_companion_client_sync_and_live_events() {
  int srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(srv_fd >= 0);
  int opt = 1;
  setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  assert(::bind(srv_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
  assert(::listen(srv_fd, 1) == 0);

  socklen_t alen = sizeof(addr);
  assert(::getsockname(srv_fd, reinterpret_cast<struct sockaddr*>(&addr), &alen) == 0);
  uint16_t port = ntohs(addr.sin_port);

  const uint8_t expected_token[16] = {
      0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
      0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01};

  std::thread server_thread([srv_fd, &expected_token]() {
    int cli = ::accept(srv_fd, nullptr, nullptr);
    assert(cli >= 0);

    std::vector<uint8_t> cmd;
    (void)expected_token;

    // Loop handling sync & DM commands
    while (recvClientFrame(cli, cmd)) {
      if (cmd.empty()) break;
      uint8_t op = cmd[0];
      if (op == CMD_DEVICE_QUERY) {
        sendRadioFrame(cli, {RESP_CODE_DEVICE_INFO, 3});
      } else if (op == CMD_GET_BATT_AND_STORAGE) {
        sendRadioFrame(cli, {RESP_CODE_BATT_AND_STORAGE, 0x68, 0x10});  // 4200 mV
      } else if (op == CMD_APP_START) {
        std::vector<uint8_t> self(58 + 11, 0);
        self[0] = RESP_CODE_SELF_INFO;
        self[2] = 20;  // tx_power = 20 dBm
        self[4] = 0xAA;
        writeU32LE(&self[36], 46020000);   // lat_e6
        writeU32LE(&self[40], 7749000);    // lon_e6
        writeU32LE(&self[48], 869525000);  // freq_hz
        writeU32LE(&self[52], 250000);     // bw_hz
        self[56] = 11;                     // sf
        self[57] = 5;                      // cr
        memcpy(&self[58], "Heltec-GW01", 11);
        sendRadioFrame(cli, self);
      } else if (op == CMD_GET_CONTACTS) {
        sendRadioFrame(cli, {RESP_CODE_CONTACTS_START, 0, 0, 0, 1});
        std::vector<uint8_t> c(148, 0);
        c[0] = RESP_CODE_CONTACT;
        c[1] = 0x11;
        c[2] = 0x22;
        c[3] = 0x33;
        c[4] = 0x44;
        c[5] = 0x55;
        c[6] = 0x66;
        c[33] = 1;  // ChatNode
        c[35] = 1;  // 1 hop
        strcpy(reinterpret_cast<char*>(&c[100]), "Relay-MonteRosa");
        writeU32LE(&c[132], 1700000100);
        writeU32LE(&c[136], 45956000);
        writeU32LE(&c[140], 7866000);
        sendRadioFrame(cli, c);
        sendRadioFrame(cli, {RESP_CODE_END_OF_CONTACTS});
      } else if (op == CMD_GET_CHANNEL) {
        uint8_t idx = cmd[1];
        std::vector<uint8_t> ch(50, 0);
        ch[0] = RESP_CODE_CHANNEL_INFO;
        ch[1] = idx;
        if (idx == 0) {
          strcpy(reinterpret_cast<char*>(&ch[2]), "Public");
        }
        sendRadioFrame(cli, ch);
      } else if (op == CMD_SYNC_NEXT_MESSAGE) {
        static bool sent_one = false;
        if (!sent_one) {
          sent_one = true;
          const char* txt = "Hello from Monte Rosa over TCP!";
          std::vector<uint8_t> dm(16 + strlen(txt), 0);
          dm[0] = RESP_CODE_CONTACT_MSG_RECV_V3;
          dm[1] = 28;  // SNR +7.0 dB
          dm[4] = 0x11;
          dm[5] = 0x22;
          dm[6] = 0x33;
          dm[7] = 0x44;
          dm[8] = 0x55;
          dm[9] = 0x66;
          dm[10] = 1;
          writeU32LE(&dm[12], 1700000200);
          memcpy(&dm[16], txt, strlen(txt));
          sendRadioFrame(cli, dm);
        } else {
          sendRadioFrame(cli, {RESP_CODE_NO_MORE_MESSAGES});
        }
      } else if (op == CMD_SEND_TXT_MSG) {
        std::vector<uint8_t> sent(10, 0);
        sent[0] = RESP_CODE_SENT;
        writeU32LE(&sent[2], 0xCAFEBABE);
        sendRadioFrame(cli, sent);

        // Also push live PUSH_CODE_SEND_CONFIRMED (0x82)
        std::vector<uint8_t> ack(9, 0);
        ack[0] = PUSH_CODE_SEND_CONFIRMED;
        writeU32LE(&ack[1], 0xCAFEBABE);
        writeU32LE(&ack[5], 145);
        sendRadioFrame(cli, ack);
        break;
      }
    }
    ::close(cli);
  });

  AppStore store;
  MeshEventBus bus;
  CompanionClientMeshActions client(store, bus);
  assert(client.connectTcp("127.0.0.1", port, expected_token));
  client.syncStore();

  MeshEvent ev{};
  while (bus.pop(ev)) {
    store.applyEvent(ev, 100);
  }

  assert(strcmp(store.status().node_name, "Heltec-GW01") == 0);
  assert(fabsf(store.radio().freq_mhz - 869.525f) < 0.01f);
  assert(store.contactCount() == 1);
  assert(strcmp(store.contacts()[0].name, "Relay-MonteRosa") == 0);
  assert(store.channelCount() == 1);
  assert(strcmp(store.channels()[0].name, "Public") == 0);
  assert(store.messageCount() == 1);
  assert(strcmp(store.messageAt(0).text, "Hello from Monte Rosa over TCP!") == 0);

  // Send a DM and poll live PUSH_CODE_SEND_CONFIRMED
  uint32_t ack_crc = 0;
  DeliveryState st = DeliveryState::Queued;
  assert(client.sendDirectMessage(store.contacts()[0].pub_key, "Copy that!", ack_crc, st));
  assert(ack_crc == 0xCAFEBABE);

  MessageRecord out_msg{};
  out_msg.is_channel = false;
  out_msg.outgoing = true;
  out_msg.ack_crc = ack_crc;
  out_msg.delivery_state = st;
  memcpy(out_msg.peer_prefix, store.contacts()[0].pub_key, 6);
  strncpy(out_msg.text, "Copy that!", sizeof(out_msg.text) - 1);
  store.appendMessage(out_msg);

  usleep(30000);
  client.poll();
  while (bus.pop(ev)) {
    store.applyEvent(ev, 200);
  }
  assert(store.messageAt(store.messageCount() - 1).delivery_state == DeliveryState::DeliveredAck);

  client.disconnect();
  server_thread.join();
  ::close(srv_fd);
}

}  // namespace

int main() {
  test_companion_client_sync_and_live_events();
  printf("[test_companion_client] ALL TESTS PASSED\n");
  return 0;
}
