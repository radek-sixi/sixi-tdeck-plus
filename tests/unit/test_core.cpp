// SPDX-License-Identifier: Apache-2.0
// tests/unit/test_core.cpp — Host unit tests for Faro app/core and pure HAL helpers (T1)
#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/Types.h"
#include "core/EventBus.h"
#include "core/AppStore.h"
#include "core/GeoMath.h"
#include "core/RepeatRoamService.h"
#include "core/OfflineTileReader.h"
#include "core/Random.h"
#include "core/Sha256.h"
#include "hal/PowerBoard.h"

using namespace faro::core;

namespace {

void testEventRing() {
  EventRing<MeshEvent, 8> ring;
  assert(ring.empty());
  assert(ring.size() == 0);

  // Capacity is 8, usable slots in power-of-2 SPSC ring is 7
  for (int i = 0; i < 7; ++i) {
    MeshEvent ev{};
    ev.type = MeshEventType::QueueSizeChanged;
    ev.queue_size = uint16_t(i + 1);
    assert(ring.push(ev));
  }
  assert(ring.size() == 7);

  // 8th push should fail and increment droppedCount
  MeshEvent extra{};
  assert(!ring.push(extra));
  assert(ring.droppedCount() == 1);

  // Pop all 7 and verify FIFO order
  for (int i = 0; i < 7; ++i) {
    MeshEvent out{};
    assert(ring.pop(out));
    assert(out.queue_size == uint16_t(i + 1));
  }
  assert(ring.empty());

  // Wrap-around push/pop
  for (int i = 0; i < 5; ++i) {
    MeshEvent ev{};
    ev.ack_crc = uint32_t(100 + i);
    assert(ring.push(ev));
  }
  for (int i = 0; i < 5; ++i) {
    MeshEvent out{};
    assert(ring.pop(out));
    assert(out.ack_crc == uint32_t(100 + i));
  }
  printf("[PASS] testEventRing\n");
}

void testRadioConfigValidator() {
  RadioConfig def{};
  assert(fabsf(def.freq_mhz - 869.618f) < 0.001f);
  assert(fabsf(def.bw_khz - 62.5f) < 0.01f);
  assert(def.sf == 8);
  assert(def.cr == 8);
  assert(def.airtime_factor >= 9.0f);
  assert(RadioConfig::isValid(def));

  static constexpr float kValid[] = {
    7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f
  };
  for (float bw : kValid) assert(RadioConfig::isValidSx1262Bandwidth(bw));
  // Invalid bandwidths that trigger RadioLib -8 (RADIOLIB_ERR_INVALID_BANDWIDTH)
  assert(!RadioConfig::isValidSx1262Bandwidth(200.0f));
  assert(!RadioConfig::isValidSx1262Bandwidth(100.0f));
  assert(!RadioConfig::isValidSx1262Bandwidth(0.0f));

  // Band policy: the whole channel must lie in 869.400-869.650 MHz
  RadioConfig eu_default{};
  eu_default.freq_mhz = 869.525f;
  eu_default.bw_khz = 250.0f;
  assert(RadioConfig::isValid(eu_default));
  RadioConfig too_wide{};
  too_wide.freq_mhz = 869.525f;
  too_wide.bw_khz = 500.0f;  // 869.275-869.775 leaves the band
  assert(!RadioConfig::isValid(too_wide));
  RadioConfig edge{};
  edge.freq_mhz = 869.618f;
  edge.bw_khz = 125.0f;      // 869.5555-869.6805 leaves the band at the top
  assert(!RadioConfig::isValid(edge));
  for (float f : {433.0f, 864.0f, 868.1f, 869.875f, 915.0f, 510.0f}) {
    RadioConfig out{};
    out.freq_mhz = f;
    assert(!RadioConfig::isValid(out));
  }
  const RadioRegion& eu = RegionPolicy::get(RadioRegionId::EU868);
  for (size_t i = 0; i < eu.preset_count; ++i) {
    RadioConfig pr{};
    pr.freq_mhz = eu.presets[i].freq_mhz;
    pr.bw_khz = eu.presets[i].bw_khz;
    pr.sf = eu.presets[i].sf;
    pr.cr = eu.presets[i].cr;
    assert(RadioConfig::isValid(pr));
  }

  // e.r.p. limit (27 dBm) and duty cycle (10 %)
  assert(RadioBandPolicy::maxConductedDbm(3.0f) == 22);   // PA limit wins
  assert(RadioBandPolicy::maxConductedDbm(10.0f) == 19);  // 27 + 2.15 - 10
  assert(fabsf(RadioBandPolicy::dutyCyclePercent(9.0f) - 10.0f) < 0.01f);
  RadioConfig hot{};
  hot.tx_power_dbm = 30;
  hot.airtime_factor = 1.0f;  // upstream default = 50 % duty
  assert(!RadioConfig::isValid(hot));
  RadioConfig fixed = RadioConfig::sanitized(hot);
  assert(fixed.tx_power_dbm == 22 && fixed.airtime_factor >= 9.0f);
  assert(RadioConfig::isValid(fixed));

  // Client repeat gated on allowed repeat frequencies
  RadioConfig bad_rep{};
  bad_rep.freq_mhz = 869.500f;
  bad_rep.client_repeat = true;
  assert(!RadioConfig::isValid(bad_rep));

  RadioConfig ok_rep{};
  ok_rep.freq_mhz = 869.525f;
  ok_rep.client_repeat = true;
  assert(RadioConfig::isValid(ok_rep));

  RadioConfig bad_sf{};
  bad_sf.sf = 13;
  assert(!RadioConfig::isValid(bad_sf));

  RadioConfig bad_cr{};
  bad_cr.cr = 9;
  assert(!RadioConfig::isValid(bad_cr));
  printf("[PASS] testRadioConfigValidator\n");
}

void testSecretsAndParsers() {
  // Weak BLE PINs are refused; random PINs are never weak
  assert(isWeakBlePin(123456) && isWeakBlePin(654321) && isWeakBlePin(111111) && isWeakBlePin(99999));
  assert(!isWeakBlePin(482915));
  for (int i = 0; i < 200; ++i) {
    uint32_t pin = randomBlePin();
    assert(pin >= 100000 && pin <= 999999 && !isWeakBlePin(pin));
  }
  // Random bytes differ between calls
  uint8_t a[16] = {0}, b[16] = {0};
  fillRandom(a, sizeof(a));
  fillRandom(b, sizeof(b));
  assert(memcmp(a, b, sizeof(a)) != 0);

  // SHA-256 test vectors (FIPS 180-4)
  uint8_t d[32];
  sha256(reinterpret_cast<const uint8_t*>("abc"), 3, d);
  assert(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad);
  sha256(reinterpret_cast<const uint8_t*>(""), 0, d);
  assert(d[0] == 0xe3 && d[31] == 0x55);
  const char* longmsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(reinterpret_cast<const uint8_t*>(longmsg), strlen(longmsg), d);
  assert(d[0] == 0x24 && d[1] == 0x8d && d[31] == 0xc1);

  // hexToBytes stops at the terminator
  uint8_t out[4];
  assert(!AppStore::hexToBytes("abc", out, 4));
  assert(AppStore::hexToBytes("0aFF10ee", out, 4) && out[1] == 0xFF);
  printf("[PASS] testSecretsAndParsers\n");
}

void testAppStoreMessagingAndRetries() {
  AppStore store;
  assert(store.channelCount() == 1);  // Default Public channel 0

  // Discover a peer with a 2-hop direct path
  MeshEvent disc{};
  disc.type = MeshEventType::ContactDiscovered;
  disc.pub_key[0] = 0xAA;
  disc.pub_key[1] = 0xBB;
  strncpy(disc.name, "MonteRosa-Peer", sizeof(disc.name) - 1);
  disc.role = NodeRole::ChatNode;
  disc.path_len = 2;
  disc.path[0] = 0x11;
  disc.path[1] = 0x22;
  disc.snr_q4 = 36;  // +9.0 dB
  disc.rssi = -78;
  store.applyEvent(disc, 1000);

  const ContactRecord* c = store.findContactByPubKey(disc.pub_key);
  assert(c != nullptr);
  assert(c->out_path_len == 2);

  // Incoming DM increments unread count
  MeshEvent dm_in{};
  dm_in.type = MeshEventType::DirectMessageRecv;
  memcpy(dm_in.pub_key, disc.pub_key, 32);
  strncpy(dm_in.name, "MonteRosa-Peer", sizeof(dm_in.name) - 1);
  strncpy(dm_in.text, "Hello from Gnifetti Hut!", sizeof(dm_in.text) - 1);
  dm_in.timestamp = 1700000100;
  store.applyEvent(dm_in, 2000);
  assert(store.totalUnreadDm() == 1);
  store.clearContactUnread(disc.pub_key);
  assert(store.totalUnreadDm() == 0);

  // Outgoing DM ACK matching
  MessageRecord out1{};
  memcpy(out1.peer_prefix, disc.pub_key, 6);
  strncpy(out1.sender_name, "Me", sizeof(out1.sender_name) - 1);
  strncpy(out1.text, "Copy that!", sizeof(out1.text) - 1);
  out1.sent_ms = 3000;
  out1.ack_crc = 0xDEADBEEF;
  out1.delivery_state = DeliveryState::SentDirect;
  out1.outgoing = true;
  store.appendMessage(out1);

  MeshEvent ack{};
  ack.type = MeshEventType::AckRecv;
  ack.ack_crc = 0xDEADBEEF;
  store.applyEvent(ack, 3500);
  assert(store.messageAt(1).delivery_state == DeliveryState::DeliveredAck);

  // Outgoing DM timeout -> 3 direct retries -> fallback to flood + path reset -> failed
  MessageRecord out2{};
  memcpy(out2.peer_prefix, disc.pub_key, 6);
  strncpy(out2.text, "Unacked msg", sizeof(out2.text) - 1);
  out2.sent_ms = 10000;
  out2.ack_crc = 0x12345678;
  out2.delivery_state = DeliveryState::SentDirect;
  out2.outgoing = true;
  store.appendMessage(out2);

  // Every timeout queues a real retransmission for the actions layer to send
  const uint32_t out2_id = store.messageAt(2).id;
  uint32_t resend_id = 0;
  store.tickMessageTimeouts(16500);  // retry 1
  assert(store.takeResendRequest(resend_id) && resend_id == out2_id);
  assert(!store.takeResendRequest(resend_id));
  store.tickMessageTimeouts(23000);  // retry 2
  assert(store.takeResendRequest(resend_id) && resend_id == out2_id);
  store.tickMessageTimeouts(29500);  // retry 3 -> path reset, next attempt floods
  assert(store.takeResendRequest(resend_id) && resend_id == out2_id);
  assert(store.findContactByPubKey(disc.pub_key)->out_path_len == 0xFF);
  store.findMessageByIdMutable(out2_id)->delivery_state = DeliveryState::SentFlood;  // as resent
  store.tickMessageTimeouts(36000);  // flood timeout -> Failed, nothing more to send
  assert(store.messageAt(2).delivery_state == DeliveryState::Failed);
  assert(!store.takeResendRequest(resend_id));

  printf("[PASS] testAppStoreMessagingAndRetries\n");
}

void testQrUriRoundTrip() {
  ContactRecord c{};
  for (int i = 0; i < 32; ++i) c.pub_key[i] = uint8_t(i + 1);
  strncpy(c.name, "Margherita_Hut", sizeof(c.name) - 1);
  c.role = NodeRole::RoomServer;
  c.valid = true;

  char uri[256] = {0};
  assert(AppStore::formatContactUri(c, uri, sizeof(uri)));

  ContactRecord parsed{};
  assert(AppStore::parseContactUri(uri, parsed));
  assert(strcmp(parsed.name, "Margherita_Hut") == 0);
  assert(parsed.role == NodeRole::RoomServer);
  assert(memcmp(parsed.pub_key, c.pub_key, 32) == 0);

  // Reject over-long public_key hex (must be exactly 64 hex chars)
  assert(!AppStore::parseContactUri(
      "meshcore://contact/add?name=Bad&public_key=0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20AA&type=1",
      parsed));

  ChannelRecord ch{};
  strncpy(ch.name, "#valais-rescue", sizeof(ch.name) - 1);
  for (int i = 0; i < 16; ++i) ch.secret[i] = uint8_t(0xA0 + i);
  ch.secret_len = 16;
  ch.valid = true;

  char ch_uri[256] = {0};
  assert(AppStore::formatChannelUri(ch, ch_uri, sizeof(ch_uri)));

  ChannelRecord parsed_ch{};
  assert(AppStore::parseChannelUri(ch_uri, parsed_ch));
  assert(strcmp(parsed_ch.name, "#valais-rescue") == 0);
  assert(parsed_ch.secret_len == 16);
  assert(memcmp(parsed_ch.secret, ch.secret, 16) == 0);
  printf("[PASS] testQrUriRoundTrip\n");
}

void testRepeatRoamService() {
  RepeatRoamService rr;
  rr.setEnabled(true);

  uint8_t repA[32] = {0x01};
  uint8_t repB[32] = {0x02};
  uint8_t chosen[32] = {0};

  rr.onRepeaterHeard(repA, "Repeater-Valley", 16 /* +4 dB */, -92, 46020000, 7740000, 1000);
  assert(rr.evaluate(1000, chosen));
  assert(chosen[0] == 0x01);
  assert(strcmp(rr.preferredName(), "Repeater-Valley") == 0);

  // Repeater B is much stronger (+11 dB, -70 dBm), but within 30s cooldown -> must NOT switch yet
  rr.onRepeaterHeard(repB, "Repeater-Ridge", 44 /* +11 dB */, -70, 45980000, 7650000, 5000);
  assert(!rr.evaluate(5000, chosen));
  assert(rr.preferredPubKey()[0] == 0x01);

  // After 30s cooldown, Repeater B exceeds hysteresis -> switches to B!
  rr.onRepeaterHeard(repA, "Repeater-Valley", 16, -92, 46020000, 7740000, 32000);
  rr.onRepeaterHeard(repB, "Repeater-Ridge", 44, -70, 45980000, 7650000, 32000);
  assert(rr.evaluate(32000, chosen));
  assert(chosen[0] == 0x02);
  assert(strcmp(rr.preferredName(), "Repeater-Ridge") == 0);
  printf("[PASS] testRepeatRoamService\n");
}

void testGeoMathAndTiles() {
  // Zermatt (46.0207, 7.7491) to Hörnlihütte (45.9826, 7.6610) ~ 8.05 km SW
  double dist = geomath::distanceMeters(46.0207, 7.7491, 45.9826, 7.6610);
  assert(dist > 7800.0 && dist < 8300.0);

  double brng = geomath::bearingDegrees(46.0207, 7.7491, 45.9826, 7.6610);
  assert(brng > 220.0 && brng < 250.0);
  assert(strcmp(geomath::cardinalDirection(brng), "SW") == 0);

  int tx = 0, ty = 0, px = 0, py = 0;
  geomath::latLonToTilePixel(46.0207, 7.7491, 12, tx, ty, px, py);
  assert(tx == 2136);
  assert(ty == 1456);

  OfflineTileReader reader;
  char path[96] = {0};
  OfflineTileReader::formatTilePath(12, tx, ty, path, sizeof(path));
  assert(strcmp(path, "/faro/maps/12/2136/1456.png") == 0);

  // Reject out-of-bounds zoom or negative tile coords
  OfflineTileReader::formatTilePath(-1, tx, ty, path, sizeof(path));
  assert(path[0] == '\0');
  OfflineTileReader::formatTilePath(19, tx, ty, path, sizeof(path));
  assert(path[0] == '\0');

  // Verify LRU cache hit on repeated lookup
  (void)reader.hasTile(12, tx, ty);
  assert(reader.cacheMisses() == 1 && reader.cacheHits() == 0);
  (void)reader.hasTile(12, tx, ty);
  assert(reader.cacheMisses() == 1 && reader.cacheHits() == 1);

  uint16_t viewport[64 * 48] = {0};
  reader.renderViewportRgb565(46.0207, 7.7491, 12, viewport, 64, 48, false);
  assert(viewport[0] != 0);
  assert(!reader.lastRenderUsedSdTiles());

  // Verify real SD tile decoding + parent-tile overzoom fallback (Switzerland default 46.8182, 8.2275)
  class MockSwissTileFs : public ITileFileSystem {
  public:
    int32_t readTileFile(const char*, uint8_t*, size_t) override { return -1; }
    bool readTileRgb565(int z, int x, int y, uint16_t* out_256x256) override {
      int ch_x = 0, ch_y = 0, p_x = 0, p_y = 0;
      geomath::latLonToTilePixel(46.8182, 8.2275, 12, ch_x, ch_y, p_x, p_y);
      if (z == 12 && x == ch_x && y == ch_y) {
        for (int i = 0; i < 256 * 256; ++i) out_256x256[i] = 0x3666;
        return true;
      }
      return false;
    }
    uint32_t availableZoomMask() const override { return (1U << 12); }
    bool getDefaultTileCoord(int& oz, int& ox, int& oy) const override {
      int px = 0, py = 0;
      geomath::latLonToTilePixel(46.8182, 8.2275, 12, ox, oy, px, py);
      oz = 12;
      return true;
    }
  } mock_fs;

  OfflineTileReader sd_reader(&mock_fs);
  sd_reader.renderViewportRgb565(46.8182, 8.2275, 12, viewport, 64, 48, false);
  assert(sd_reader.lastRenderUsedSdTiles());
  assert(viewport[(24 * 64) + 32] == 0x3666);

  // Overzoom at Z13 should sample parent tile from Z12
  memset(viewport, 0, sizeof(viewport));
  sd_reader.renderViewportRgb565(46.8182, 8.2275, 13, viewport, 64, 48, false);
  assert(sd_reader.lastRenderUsedSdTiles());
  assert(viewport[(24 * 64) + 32] == 0x3666);

  printf("[PASS] testGeoMathAndTiles\n");
}


void testPowerBoardCurve() {
  faro::hal::PowerBoard pb;
  assert(pb.readBattPercent(3200) == 0);
  assert(pb.readBattPercent(3300) == 0);
  assert(pb.readBattPercent(3700) == 20);
  assert(pb.readBattPercent(3950) == 60);
  assert(pb.readBattPercent(4200) == 100);
  assert(pb.readBattPercent(4300) == 100);
  printf("[PASS] testPowerBoardCurve\n");
}

}  // namespace

void testRadioRegions() {
  // Every preset fits its region at the region's power ceiling and default duty cycle
  for (size_t ri = 0; ri < kNumRadioRegions; ++ri) {
    const RadioRegion& reg = kRadioRegions[ri];
    assert(size_t(reg.id) == ri);
    assert(reg.preset_count > 0);
    for (size_t i = 0; i < reg.preset_count; ++i) {
      RadioConfig pr{};
      pr.freq_mhz = reg.presets[i].freq_mhz;
      pr.bw_khz = reg.presets[i].bw_khz;
      pr.sf = reg.presets[i].sf;
      pr.cr = reg.presets[i].cr;
      pr.tx_power_dbm = RegionPolicy::maxConductedDbm(reg);
      pr.airtime_factor = reg.default_airtime_factor;
      assert(RadioConfig::isValid(pr, reg));
      assert(RegionPolicy::findPreset(reg, pr.freq_mhz, pr.bw_khz, pr.sf, pr.cr) >= 0);
    }
    for (size_t i = 0; i < reg.repeat_count; ++i) {
      assert(RegionPolicy::channelInRegion(reg, reg.repeat_freqs[i], 62.5f));
    }
  }

  // Power ceilings: e.r.p. adds 2.15 dB, e.i.r.p. does not; the PA limit (22 dBm) wins
  const RadioRegion& eu = RegionPolicy::get(RadioRegionId::EU868);
  const RadioRegion& eu433 = RegionPolicy::get(RadioRegionId::EU433);
  const RadioRegion& us = RegionPolicy::get(RadioRegionId::US915);
  const RadioRegion& anz = RegionPolicy::get(RadioRegionId::ANZ915);
  const RadioRegion& br = RegionPolicy::get(RadioRegionId::BR915);
  assert(RegionPolicy::maxConductedDbm(eu, 3.0f) == 22);
  assert(RegionPolicy::maxConductedDbm(eu433, 3.0f) == 9);    // 10 + 2.15 - 3
  assert(RegionPolicy::maxConductedDbm(eu433, 10.0f) == 2);
  assert(RegionPolicy::maxConductedDbm(us, 3.0f) == 22);
  assert(RegionPolicy::maxConductedDbm(anz, 10.0f) == 20);    // 30 - 10
  assert(fabsf(RegionPolicy::radiatedDbm(anz, 20, 10.0f) - 30.0f) < 0.01f);

  // Bands: Brazil has a gap between 907.5 and 915 MHz
  assert(RegionPolicy::channelInRegion(br, 905.0f, 62.5f));
  assert(!RegionPolicy::channelInRegion(br, 910.0f, 62.5f));
  assert(RegionPolicy::channelInRegion(br, 923.125f, 62.5f));
  assert(!RegionPolicy::channelInRegion(us, 927.95f, 125.0f));  // upper edge
  assert(!RegionPolicy::channelInRegion(eu433, 433.0f, 62.5f)); // below the ISM band

  // Region detection: preferred region first, then a matching preset, then any band
  RadioRegionId found = RadioRegionId::EU868;
  assert(RegionPolicy::findRegionFor(869.618f, 62.5f, RadioRegionId::US915, found) && found == RadioRegionId::EU868);
  assert(RegionPolicy::findRegionFor(910.525f, 62.5f, RadioRegionId::EU868, found) && found == RadioRegionId::US915);
  assert(RegionPolicy::findRegionFor(917.375f, 62.5f, RadioRegionId::EU868, found) && found == RadioRegionId::ANZ915);
  assert(RegionPolicy::findRegionFor(917.375f, 62.5f, RadioRegionId::US915, found) && found == RadioRegionId::US915);
  assert(RegionPolicy::findRegionFor(433.650f, 250.0f, RadioRegionId::EU868, found) && found == RadioRegionId::EU433);
  assert(!RegionPolicy::findRegionFor(868.100f, 125.0f, RadioRegionId::EU868, found));
  assert(!RegionPolicy::findRegionFor(2450.0f, 125.0f, RadioRegionId::EU868, found));

  // Duty cycle: EU keeps af >= 9 (10 %), the 915 regions have no duty limit
  RadioConfig fast{};
  fast.freq_mhz = 910.525f;
  fast.bw_khz = 62.5f;
  fast.airtime_factor = 1.0f;
  assert(RadioConfig::isValid(fast, us));
  assert(!RadioConfig::isValid(fast, eu));
  RadioConfig eu_fast{};
  eu_fast.airtime_factor = 1.0f;
  assert(RadioConfig::sanitized(eu_fast, eu).airtime_factor >= 9.0f);
  assert(fabsf(RadioConfig::sanitized(fast, us).airtime_factor - 1.0f) < 0.001f);

  // Picking a region applies its first preset, its ceiling and its default duty cycle
  uint8_t hash = 9;
  RadioConfig def = RadioConfig::regionDefault(us, RadioConfig{}, hash);
  assert(fabsf(def.freq_mhz - 910.525f) < 0.001f && def.tx_power_dbm == 22 && hash == 0);
  assert(fabsf(def.airtime_factor - 1.0f) < 0.001f && RadioConfig::isValid(def, us));
  def = RadioConfig::regionDefault(eu433, RadioConfig{}, hash);
  assert(def.tx_power_dbm == 9 && def.airtime_factor >= 9.0f && RadioConfig::isValid(def, eu433));

  // Client repeat only on the region's repeat frequencies (none in EU 433)
  RadioConfig rep{};
  rep.freq_mhz = 918.0f;
  rep.client_repeat = true;
  rep.airtime_factor = 1.0f;
  assert(RadioConfig::isValid(rep, us));
  rep.freq_mhz = 433.650f;
  assert(!RadioConfig::isValid(rep, eu433));
  assert(!RadioConfig::sanitized(rep, eu433).client_repeat);

  // The store keeps the region
  AppStore store;
  assert(store.region() == RadioRegionId::EU868);
  assert(store.setRegion(RadioRegionId::ANZ915) && store.region() == RadioRegionId::ANZ915);
  assert(!store.setRegion(RadioRegionId(42)));
  printf("[PASS] testRadioRegions\n");
}

void testPathEncodingAndTrace() {
  ContactRecord c{};
  assert(!c.hasPath() && c.pathHops() == 0 && c.pathByteLen() == 0);
  c.out_path_len = ContactRecord::encodePathLen(2, 3);  // 3 hops of 2-byte hashes
  assert(c.out_path_len == 67 && c.hasPath() && c.pathHops() == 3 && c.pathHashSize() == 2 && c.pathByteLen() == 6);

  // 1-byte path A1,B2 to a repeater T: A1 B2 T B2 A1
  const uint8_t target[32] = {0x7E, 0x7F};
  const uint8_t p1[64] = {0xA1, 0xB2};
  uint8_t path[64] = {0};
  uint8_t flags = 0xFF;
  size_t n = AppStore::buildTracePath(p1, ContactRecord::encodePathLen(1, 2), target, true, path, sizeof(path), flags);
  const uint8_t want1[5] = {0xA1, 0xB2, 0x7E, 0xB2, 0xA1};
  assert(n == 5 && flags == 0 && memcmp(path, want1, 5) == 0);
  // Same path to a chat node (does not forward): A1 B2 A1
  n = AppStore::buildTracePath(p1, ContactRecord::encodePathLen(1, 2), target, false, path, sizeof(path), flags);
  const uint8_t want2[3] = {0xA1, 0xB2, 0xA1};
  assert(n == 3 && memcmp(path, want2, 3) == 0);
  // 2-byte hashes: trace hashes are 2 bytes (flags 1)
  const uint8_t p2[64] = {0xA1, 0xA2};
  n = AppStore::buildTracePath(p2, ContactRecord::encodePathLen(2, 1), target, true, path, sizeof(path), flags);
  const uint8_t want3[6] = {0xA1, 0xA2, 0x7E, 0x7F, 0xA1, 0xA2};
  assert(n == 6 && flags == 1 && memcmp(path, want3, 6) == 0);
  // Nothing to trace: unknown path, or a direct neighbour that does not forward
  assert(AppStore::buildTracePath(p1, 0xFF, target, true, path, sizeof(path), flags) == 0);
  assert(AppStore::buildTracePath(p1, ContactRecord::encodePathLen(1, 0), target, false, path, sizeof(path), flags) == 0);
  assert(AppStore::buildTracePath(p1, ContactRecord::encodePathLen(1, 0), target, true, path, sizeof(path), flags) == 1);
  printf("[PASS] testPathEncodingAndTrace\n");
}

void testRemoteResponses() {
  AppStore store;
  ContactRecord rpt{};
  rpt.pub_key[0] = 0x42;
  rpt.pub_key[1] = 0x43;
  strncpy(rpt.name, "Ridge-RPT", sizeof(rpt.name) - 1);
  rpt.role = NodeRole::Repeater;
  rpt.valid = true;
  store.upsertContact(rpt);
  ContactRecord nb{};
  nb.pub_key[0] = 0x11;
  nb.pub_key[1] = 0x22;
  nb.pub_key[2] = 0x33;
  nb.pub_key[3] = 0x44;
  strncpy(nb.name, "Valley", sizeof(nb.name) - 1);
  nb.valid = true;
  store.upsertContact(nb);

  // Status (RepeaterStats, little endian)
  uint8_t st[56] = {0};
  const uint16_t batt = 4012;
  const int16_t noise = -110, rssi = -80, snr_q4 = 20;
  const uint32_t up = 7200;
  memcpy(&st[0], &batt, 2);
  memcpy(&st[4], &noise, 2);
  memcpy(&st[6], &rssi, 2);
  memcpy(&st[20], &up, 4);
  memcpy(&st[42], &snr_q4, 2);
  store.applyRemoteResponse(RemoteRequestKind::Status, rpt.pub_key, st, sizeof(st));
  assert(strstr(store.status().remote_admin_reply, "4012mV") != nullptr);
  assert(strstr(store.status().remote_admin_reply, "SNR +5dB") != nullptr);
  assert(store.findContactByPubKey(rpt.pub_key)->remote_batt_mv == 4012);

  // Telemetry: Cayenne LPP voltage 4.10 V (channel 1) and temperature 22.5 C
  const uint8_t lpp[] = {1, 116, 0x01, 0x9A, 1, 103, 0x00, 0xE1};
  store.applyRemoteResponse(RemoteRequestKind::Telemetry, rpt.pub_key, lpp, sizeof(lpp));
  assert(store.findContactByPubKey(rpt.pub_key)->remote_batt_mv == 4100);
  assert(store.findContactByPubKey(rpt.pub_key)->remote_temp_c == 22);
  assert(strstr(store.status().remote_admin_reply, "4.10V") != nullptr);

  // Neighbours: total 1, count 1, [prefix 4][secs ago][snr x4]
  uint8_t nbr[4 + 9] = {1, 0, 1, 0, 0x11, 0x22, 0x33, 0x44, 120, 0, 0, 0, uint8_t(int8_t(-12))};
  store.applyRemoteResponse(RemoteRequestKind::Neighbours, rpt.pub_key, nbr, sizeof(nbr));
  assert(strstr(store.status().remote_admin_reply, "Valley") != nullptr);
  assert(strstr(store.status().remote_admin_reply, "-3dB") != nullptr);

  // Login answer: [OK][keep-alive][is_admin][permissions]
  const uint8_t login_ok[4] = {0, 0, 1, 3};
  store.applyRemoteResponse(RemoteRequestKind::Login, rpt.pub_key, login_ok, sizeof(login_ok));
  assert(store.status().remote_logged_in && strstr(store.status().remote_admin_reply, "admin") != nullptr);
  const uint8_t login_bad[1] = {1};
  store.applyRemoteResponse(RemoteRequestKind::Login, rpt.pub_key, login_bad, sizeof(login_bad));
  assert(!store.status().remote_logged_in);

  // A RemoteResponse event mirrors the path MeshCore learned
  MeshEvent ev{};
  ev.type = MeshEventType::RemoteResponse;
  ev.resp_kind = uint8_t(RemoteRequestKind::PathDiscovery);
  memcpy(ev.pub_key, rpt.pub_key, 32);
  ev.path_len = ContactRecord::encodePathLen(1, 2);
  ev.path[0] = 0xC3;
  ev.path[1] = 0xD4;
  store.applyEvent(ev, 1000);
  const ContactRecord* r = store.findContactByPubKey(rpt.pub_key);
  assert(r->pathHops() == 2 && r->out_path[1] == 0xD4);

  // CLI replies go to the admin log, not into the chat history
  const size_t before = store.messageCount();
  MeshEvent cli{};
  cli.type = MeshEventType::DirectMessageRecv;
  cli.txt_type = 1;
  memcpy(cli.pub_key, rpt.pub_key, 32);
  strncpy(cli.name, rpt.name, sizeof(cli.name) - 1);
  strncpy(cli.text, "v1.18.0", sizeof(cli.text) - 1);
  store.applyEvent(cli, 2000);
  assert(store.messageCount() == before && strstr(store.status().remote_admin_reply, "v1.18.0") != nullptr);
  printf("[PASS] testRemoteResponses\n");
}

void testPersistence() {
  const char* ui_path = "/tmp/faro_test_ui.bin";
  const char* msg_path = "/tmp/faro_test_msgs.bin";
  remove(ui_path);
  remove(msg_path);

  AppStore a;
  ContactRecord c{};
  c.pub_key[0] = 0x55;
  strncpy(c.name, "Alice", sizeof(c.name) - 1);
  c.valid = true;
  a.upsertContact(c);
  ContactRecord* ac = a.findContactByPubKeyMutable(c.pub_key);
  ac->favourite = true;
  ac->tag_mask = 0x05;
  strncpy(ac->notes, "meet at the hut", sizeof(ac->notes) - 1);
  a.uiPrefs().language = Language::DE;
  a.uiPrefs().outdoor_high_contrast = false;
  a.uiPrefs().screen_timeout_secs = 300;
  a.uiPrefs().deadman_mode = 2;
  a.uiPrefs().deadman_triggered = true;
  a.setRegion(RadioRegionId::US915);
  a.channelsMutable()[0].muted = true;
  const uint32_t h1 = a.uiStateHash();
  assert(a.saveUiState(ui_path));

  MessageRecord out{};
  out.outgoing = true;
  memcpy(out.peer_prefix, c.pub_key, 6);
  strncpy(out.text, "hello", sizeof(out.text) - 1);
  out.delivery_state = DeliveryState::SentDirect;
  a.appendMessage(out);
  MessageRecord in{};
  in.is_channel = true;
  in.channel_index = 0;
  strncpy(in.sender_name, "Bob", sizeof(in.sender_name) - 1);
  strncpy(in.text, "grüezi", sizeof(in.text) - 1);
  in.delivery_state = DeliveryState::DeliveredAck;
  a.appendMessage(in);
  assert(a.saveMessages(msg_path));

  // A fresh store with the same contact (MeshCore loads it) gets everything back
  AppStore b;
  b.upsertContact(c);
  assert(b.loadUiState(ui_path));
  assert(b.uiPrefs().language == Language::DE && !b.uiPrefs().outdoor_high_contrast);
  assert(b.uiPrefs().screen_timeout_secs == 300 && b.uiPrefs().deadman_mode == 2);
  assert(!b.uiPrefs().deadman_triggered);  // a restart never fires the dead-man SOS
  assert(b.region() == RadioRegionId::US915);
  const ContactRecord* bc = b.findContactByPubKey(c.pub_key);
  assert(bc && bc->favourite && bc->tag_mask == 0x05 && strcmp(bc->notes, "meet at the hut") == 0);
  assert(b.findChannel(0)->muted);
  assert(b.uiStateHash() == h1);
  assert(b.loadMessages(msg_path));
  assert(b.messageCount() == 2);
  assert(strcmp(b.messageAt(0).text, "hello") == 0 && b.messageAt(0).outgoing);
  assert(b.messageAt(0).delivery_state == DeliveryState::Failed);  // no ACK can come after a restart
  assert(strcmp(b.messageAt(1).text, "grüezi") == 0 && strcmp(b.messageAt(1).sender_name, "Bob") == 0);
  assert(b.messagesHash() != AppStore{}.messagesHash());
  const uint32_t next_id = b.appendMessage(in);
  assert(next_id > b.messageAt(1).id);

  // Contacts that are gone are not recreated; a changed preference changes the hash
  AppStore empty;
  assert(empty.loadUiState(ui_path) && empty.contactCount() == 0);
  b.uiPrefs().kid_mode = true;
  assert(b.uiStateHash() != h1);

  // Wrong file: refused, store unchanged
  FILE* f = fopen(ui_path, "wb");
  fputs("garbage", f);
  fclose(f);
  AppStore g;
  assert(!g.loadUiState(ui_path) && g.uiPrefs().language == Language::EN);
  assert(!g.loadMessages("/tmp/faro_test_missing.bin"));
  b.clearMessages();
  assert(b.messageCount() == 0);

  // Desktop console: both parts in one file
  assert(a.saveToFile(ui_path));
  AppStore d;
  d.upsertContact(c);
  assert(d.loadFromFile(ui_path) && d.messageCount() == 2);
  remove(ui_path);
  remove(msg_path);
  printf("[PASS] testPersistence\n");
}

void testTrackRecording() {
  AppStore s;
  assert(!s.recordTrackPoint(46.0, 8.0, 500.0f, 1));  // not recording
  s.gpsTrack().recording = true;
  assert(s.recordTrackPoint(46.0, 8.0, 500.0f, 1));
  assert(!s.recordTrackPoint(46.00005, 8.0, 500.0f, 2));  // ~5.6 m: skipped
  assert(s.recordTrackPoint(46.001, 8.0, 510.0f, 3));      // ~111 m
  assert(s.gpsTrack().point_count == 2 && s.gpsTrack().distance_m > 100.0);
  for (int i = 0; i < 200; ++i) s.recordTrackPoint(46.002 + 0.001 * i, 8.0, 500.0f, uint32_t(4 + i));
  const size_t cap = sizeof(s.gpsTrack().points) / sizeof(s.gpsTrack().points[0]);
  assert(s.gpsTrack().point_count <= cap && s.gpsTrack().point_count > cap / 2);
  assert(s.gpsTrack().points[0].timestamp == 1);  // thinning keeps the start
  printf("[PASS] testTrackRecording\n");
}

int main() {
  testEventRing();
  testRadioConfigValidator();
  testRadioRegions();
  testPathEncodingAndTrace();
  testRemoteResponses();
  testPersistence();
  testTrackRecording();
  testAppStoreMessagingAndRetries();
  testQrUriRoundTrip();
  testRepeatRoamService();
  testGeoMathAndTiles();
  testSecretsAndParsers();
  testPowerBoardCurve();
  printf("ALL PHASE 2 CORE UNIT TESTS PASSED.\n");
  return 0;
}
