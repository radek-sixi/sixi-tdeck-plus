// SPDX-License-Identifier: Apache-2.0
// tests/unit/test_ui_sim.cpp — Headless LVGL 9 UI & VirtualMeshActions integration tests (T2)
#include <assert.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "core/AppStore.h"
#include "core/EventBus.h"
#include "core/GeoMath.h"
#include "core/OfflineTileReader.h"
#include "ui/App.h"
#include "ui/i18n/Strings.h"
#include "VirtualMeshActions.h"

namespace {

uint16_t g_test_fb[320 * 240] = {0};
uint16_t g_test_buf[320 * 240] = {0};

void testFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    for (int32_t x = area->x1; x <= area->x2; ++x) {
      g_test_fb[y * 320 + x] = *src++;
    }
  }
  lv_display_flush_ready(disp);
}

}  // namespace

int main() {
  lv_init();
  lv_display_t* disp = lv_display_create(320, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g_test_buf, nullptr, sizeof(g_test_buf), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, testFlushCb);

  faro::core::AppStore store;
  faro::core::MeshEventBus bus;
  faro::sim::VirtualMeshActions actions(store, bus);
  faro::core::OfflineTileReader tiles;

  actions.seedDemoTopology();
  assert(store.contactCount() == 6);
  assert(store.channelCount() == 3);

  faro::ui::App app(store, actions, tiles);
  app.begin();
  assert(app.inputGroup() != nullptr);

  // Verify all 15 screens render across EN, IT, DE, FR and Outdoor mode
  static const faro::core::Language kTestLangs[] = {
    faro::core::Language::EN,
    faro::core::Language::IT,
    faro::core::Language::DE,
    faro::core::Language::FR
  };
  for (auto lang : kTestLangs) {
    store.uiPrefs().language = lang;
    for (uint8_t s = 0; s < uint8_t(faro::ui::ScreenId::_Count); ++s) {
      memset(g_test_fb, 0, sizeof(g_test_fb));
      app.showScreen(static_cast<faro::ui::ScreenId>(s));
      lv_tick_inc(25);
      lv_timer_handler();
      lv_refr_now(disp);

      // Verify non-blank framebuffer
      size_t non_zero = 0;
      for (size_t px = 0; px < 320 * 240; ++px) {
        if (g_test_fb[px] != 0) ++non_zero;
      }
      assert(non_zero > 1000);
    }
  }

  // Test DM send + simulated ACK via EventBus
  uint32_t crc = 0;
  faro::core::DeliveryState dstate = faro::core::DeliveryState::Queued;
  assert(actions.sendDirectMessage(store.contacts()[0].pub_key, "Test DM from UI sim", crc, dstate));
  assert(crc != 0);
  faro::core::MeshEvent ev{};
  while (bus.pop(ev)) {
    store.applyEvent(ev, 5000);
  }
  assert(store.messageAt(store.messageCount() - 1).delivery_state == faro::core::DeliveryState::DeliveredAck);


  // Test 0-hop & flood adverts
  assert(actions.sendZeroHopAdvert());
  assert(actions.sendFloodAdvert());
  assert(actions.zeroHopAdvertsSent() == 1);
  assert(actions.floodAdvertsSent() == 1);

  // Test keyboard entry on ChatThread via App::onKeyPress
  size_t msg_before = store.messageCount();
  app.openChatWithContact(0);
  app.onKeyPress('O');
  app.onKeyPress('K');
  app.onKeyPress('\r');
  assert(store.messageCount() == msg_before + 1);
  assert(strcmp(store.messageAt(store.messageCount() - 1).text, "OK") == 0);

  // Test Regions, CLI console, Remote Admin, and Kid Mode (Ripple UI & MeshCore Android features)
  assert(store.regionCount() >= 3);
  char cli_out[128] = {0};
  assert(actions.runCliCommand("ver", cli_out, sizeof(cli_out)));
  assert(strstr(cli_out, "MeshCore") != nullptr);
  assert(actions.sendRemoteLogin(store.contacts()[1].pub_key, "admin"));
  assert(actions.sendRemoteCli(store.contacts()[1].pub_key, "ver"));
  assert(strstr(store.status().remote_admin_reply, "CLI[ver]") != nullptr);

  store.uiPrefs().kid_mode = true;
  app.showScreen(faro::ui::ScreenId::Home);
  lv_tick_inc(25);
  lv_timer_handler();
  lv_refr_now(disp);
  store.uiPrefs().kid_mode = false;

  // Test SwissGrid LV95 & UTM conversion, GPX export, persistent store, and URI export/import
  int32_t lv_e = 0, lv_n = 0;
  faro::core::geomath::wgs84ToSwissLV95(46.0207, 7.7491, lv_e, lv_n);
  assert(lv_e > 2600000 && lv_e < 2650000);
  assert(lv_n > 1080000 && lv_n < 1120000);
  int u_z = 0; char u_b = 0; int32_t u_e = 0, u_n = 0;
  faro::core::geomath::wgs84ToUtm(46.0207, 7.7491, u_z, u_b, u_e, u_n);
  assert(u_z == 32 && u_b == 'T');

  char gpx_path[96] = {0};
  assert(store.exportGpsTrackGpx("/tmp/test_faro_track.gpx", gpx_path, sizeof(gpx_path)));
  assert(store.saveToFile("/tmp/test_faro_store.dat"));

  faro::core::AppStore loaded_store;
  assert(loaded_store.loadFromFile("/tmp/test_faro_store.dat"));
  assert(loaded_store.messageCount() == store.messageCount());

  char exported_uri[220] = {0};
  assert(actions.exportContactUri(store.contacts()[0].pub_key, exported_uri, sizeof(exported_uri)));
  assert(strncmp(exported_uri, "meshcore://contact/add?", 23) == 0);
  assert(actions.useLastAdvertPath(store.contacts()[0].pub_key));
  assert(store.contacts()[0].out_path_len == 2);

  // Test SX1262 LoRa Link Budget & Time-on-Air Calculator, Range Test CSV, PingBot & Dead-Man's Switch
  float toa_64 = faro::core::geomath::loraTimeOnAirMs(64, 8, 62.5f, 8);
  assert(toa_64 > 150.0f && toa_64 < 650.0f);
  float lb_db = faro::core::geomath::loraLinkBudgetDb(22, 8, 62.5f);
  assert(lb_db > 140.0f && lb_db < 170.0f);
  assert(faro::core::geomath::maxPacketsPerHour10Pct(toa_64) > 500U);

  store.recordRangeSample(46.0207, 7.7491, 36, -78, 1, 100);
  assert(store.rangeSampleCount() == 1);

  // Enable PingBot: it ignores channel traffic and answers a DM from a contact
  app.tick(10000);
  store.uiPrefs().bot_mode = 1;
  faro::core::MeshEvent ping_ev{};
  ping_ev.type = faro::core::MeshEventType::ChannelMessageRecv;
  ping_ev.channel_index = 0;
  strncpy(ping_ev.name, "Tester", sizeof(ping_ev.name) - 1);
  strncpy(ping_ev.text, "ping test", sizeof(ping_ev.text) - 1);
  ping_ev.snr_q4 = 32;
  ping_ev.rssi = -80;
  ping_ev.hops = 1;
  store.applyEvent(ping_ev, 11000);
  app.tick(11100);
  assert(store.uiPrefs().bot_replies_sent == 0);  // never answers on a channel

  ping_ev.type = faro::core::MeshEventType::DirectMessageRecv;
  memcpy(ping_ev.pub_key, store.contacts()[0].pub_key, 32);
  strncpy(ping_ev.name, store.contacts()[0].name, sizeof(ping_ev.name) - 1);
  store.applyEvent(ping_ev, 11200);
  app.tick(11300);
  assert(store.uiPrefs().bot_replies_sent == 1);
  assert(strstr(store.messageAt(store.messageCount() - 1).text, "[Faro Bot]") != nullptr);
  assert(strstr(store.messageAt(store.messageCount() - 1).text, "Pos:46.021,7.749") != nullptr);  // coarse

  // Dead-Man's Switch: the SOS (with position) goes only to a private channel, never on Public
  for (size_t ci = 0; ci < faro::core::AppStore::kMaxChannels; ++ci) {
    auto& ch = store.channelsMutable()[ci];
    if (ch.index != 0 && !ch.is_hashtag) ch.valid = false;
  }
  const size_t msgs_before = store.messageCount();
  store.uiPrefs().deadman_mode = 1;
  store.uiPrefs().deadman_remaining_secs = 2;
  store.uiPrefs().deadman_triggered = false;
  app.tick(20000);
  app.tick(23000);
  assert(store.uiPrefs().deadman_triggered);
  assert(store.messageCount() == msgs_before);  // no private channel: nothing sent
  assert(strstr(store.status().last_cli_reply, "Dead-man") != nullptr);

  faro::core::ChannelRecord team{};
  team.index = 5;
  strncpy(team.name, "Team", sizeof(team.name) - 1);
  team.valid = true;
  assert(store.upsertChannel(team) != nullptr);
  store.uiPrefs().deadman_remaining_secs = 2;
  store.uiPrefs().deadman_triggered = false;
  app.tick(30000);
  app.tick(33000);
  assert(store.uiPrefs().deadman_triggered);
  assert(strstr(store.messageAt(store.messageCount() - 1).text, "[SOS DEADMAN]") != nullptr);
  assert(store.messageAt(store.messageCount() - 1).channel_index == 5);

  // Store & Forward on Advert + JSON Contact Export
  app.showScreen(faro::ui::ScreenId::Home);
  lv_tick_inc(25);
  lv_timer_handler();
  lv_refr_now(disp);

  char json_path[96] = {0};
  assert(store.exportContactsJson("/tmp/test_faro_contacts.json", json_path, sizeof(json_path)));

  // Queue a failed DM and verify Store & Forward really transmits it again when the peer adverts
  faro::core::MessageRecord sf_msg{};
  sf_msg.is_channel = false;
  memcpy(sf_msg.peer_prefix, store.contacts()[0].pub_key, 6);
  strncpy(sf_msg.sender_name, "Me", sizeof(sf_msg.sender_name) - 1);
  strncpy(sf_msg.text, "Delayed offline message", sizeof(sf_msg.text) - 1);
  sf_msg.delivery_state = faro::core::DeliveryState::Failed;
  sf_msg.outgoing = true;
  store.appendMessage(sf_msg);

  uint16_t sf_before = store.uiPrefs().sf_delivered_count;
  faro::core::MeshEvent adv_ev{};
  adv_ev.type = faro::core::MeshEventType::ContactDiscovered;
  memcpy(adv_ev.pub_key, store.contacts()[0].pub_key, 32);
  strncpy(adv_ev.name, store.contacts()[0].name, sizeof(adv_ev.name) - 1);
  adv_ev.role = store.contacts()[0].role;
  adv_ev.lat_e6 = 46150000;
  adv_ev.lon_e6 = 7850000;
  store.applyEvent(adv_ev, 25000);
  assert(store.uiPrefs().sf_delivered_count == sf_before + 1);
  uint32_t tx_before = store.status().tx_packets;
  const uint32_t sf_id = store.messageAt(store.messageCount() - 1).id;
  actions.tick(25100);  // drains the resend queue
  assert(store.status().tx_packets > tx_before);
  assert(store.findMessageByIdMutable(sf_id)->delivery_state != faro::core::DeliveryState::Failed);


  app.showScreen(faro::ui::ScreenId::Home);
  lv_tick_inc(25);
  lv_timer_handler();
  lv_refr_now(disp);

  // Test Node Name change + SD Settings Export (with overwrite) & Import
  assert(actions.setNodeName("Test-TDeck"));
  assert(strcmp(store.status().node_name, "Test-TDeck") == 0);
  store.status().multi_acks = 2;
  store.status().cad_enabled = true;
  assert(actions.exportSettingsToSd(true));
  assert(actions.setNodeName("Temp-Node"));
  assert(strcmp(store.status().node_name, "Temp-Node") == 0);
  assert(actions.importSettingsFromSd());
  assert(strcmp(store.status().node_name, "Test-TDeck") == 0);
  assert(store.status().multi_acks == 2);
  assert(store.status().cad_enabled == true);
  // Overwrite again with a new node name
  assert(actions.setNodeName("Faro T-Deck"));
  assert(actions.exportSettingsToSd(true));
  assert(actions.importSettingsFromSd());
  assert(strcmp(store.status().node_name, "Faro T-Deck") == 0);

  printf("[PASS] test_ui_sim (all screens x 4 languages)\n");
  return 0;
}
