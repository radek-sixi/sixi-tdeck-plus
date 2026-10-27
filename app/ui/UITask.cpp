// SPDX-License-Identifier: Apache-2.0
// app/ui/UITask.cpp — LVGL 9.3 app, DeviceMeshActions and input devices on the MeshCore UITask hook
#ifdef ARDUINO
#include <Arduino.h>
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#if !defined(FARO_HEADLESS)
#include <lvgl.h>
#if LV_USE_LODEPNG
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);
#endif
#include "FaroDisplay.h"
#include "TouchGT911.h"
#include "KeyboardBBQ10.h"
#include "KeypadM9.h"
#include "Trackball.h"
#include "ui/App.h"
#endif

#include "UITask.h"
#include "PowerBoard.h"
#include "AudioBuzzer.h"
#include "SdCard.h"
#include "GpsPort.h"
#include "core/AppStore.h"
#include "core/EventBus.h"
#include "core/OfflineTileReader.h"
#include "platform/DeviceMeshActions.h"

namespace faro {

namespace {

#if !defined(FARO_HEADLESS)
class SdTileFs : public core::ITileFileSystem {
public:
  static constexpr size_t kMaxTileFileBytes = 131072;  // 128 KB max raw/PNG/BMP tile file

  explicit SdTileFs(hal::SdCard& sd) : _sd(sd) {}
  ~SdTileFs() override {
    if (_file_buf) {
      free(_file_buf);
      _file_buf = nullptr;
    }
  }

  int32_t readTileFile(const char* path, uint8_t* buf, size_t max_len) override {
    return _sd.readFile(path, buf, max_len);
  }

  bool readTileRgb565(int z, int x, int y, uint16_t* out_256x256) override {
    if (!out_256x256) return false;
    if (!_file_buf) {
      if (psramFound()) {
        _file_buf = static_cast<uint8_t*>(ps_malloc(kMaxTileFileBytes));
      }
      if (!_file_buf) {
        _file_buf = static_cast<uint8_t*>(malloc(kMaxTileFileBytes));
      }
      if (!_file_buf) return false;
    }
    char ext[12] = {0};
    int32_t n = _sd.readTileFile(z, x, y, _file_buf, kMaxTileFileBytes, ext, sizeof(ext));
    if (n <= 8) return false;

#if LV_USE_LODEPNG
    // Check PNG magic signature: 89 50 4E 47 0D 0A 1A 0A
    if (_file_buf[0] == 0x89 && _file_buf[1] == 'P' && _file_buf[2] == 'N' && _file_buf[3] == 'G') {
      unsigned w = 0, h = 0;
      lv_draw_buf_t* decoded = nullptr;
      unsigned err = lodepng_decode32(reinterpret_cast<unsigned char**>(&decoded),
                                      &w, &h, _file_buf, size_t(n));
      bool ok = false;
      if (err == 0 && decoded && decoded->data && w > 0 && h > 0) {
        const uint8_t* rgba = decoded->data;
        for (int py = 0; py < 256; ++py) {
          unsigned sy = (unsigned(py) * h) / 256U;
          if (sy >= h) sy = h - 1;
          for (int px = 0; px < 256; ++px) {
            unsigned sx = (unsigned(px) * w) / 256U;
            if (sx >= w) sx = w - 1;
            const uint8_t* p = rgba + (sy * w + sx) * 4U;
            uint16_t r = p[0];
            uint16_t g = p[1];
            uint16_t b = p[2];
            out_256x256[py * 256 + px] = uint16_t(((r & 0xF8U) << 8) |
                                                  ((g & 0xFCU) << 3) |
                                                  (b >> 3));
          }
        }
        ok = true;
      }
      if (decoded) {
        lv_draw_buf_destroy(decoded);
      }
      return ok;
    }
#endif

    return core::OfflineTileReader::decodeRawOrBmpToRgb565(_file_buf, size_t(n), out_256x256);
  }

  uint32_t availableZoomMask() const override {
    return _sd.availableZoomMask();
  }

  bool getDefaultTileCoord(int& out_z, int& out_x, int& out_y) const override {
    if (!_sd.hasDiscoveredTiles() || _sd.defaultTileZ() < 0) return false;
    out_z = _sd.defaultTileZ();
    out_x = _sd.defaultTileX();
    out_y = _sd.defaultTileY();
    return true;
  }

private:
  hal::SdCard& _sd;
  uint8_t* _file_buf = nullptr;
};
#endif
}  // namespace

class DeviceController {
public:
  SensorManager* sensors = nullptr;
  NodePrefs* prefs = nullptr;

  hal::PowerBoard power;
  hal::AudioBuzzer buzzer;
  hal::SdCard sd;
  hal::GpsPort gps_port;

  core::AppStore store;
  core::MeshEventBus bus;
  core::MeshEvent pop_ev;
  platform::DeviceMeshActions actions{store, bus};


#if !defined(FARO_HEADLESS)
  FaroDisplay* display = nullptr;
  hal::TouchGT911 touch;
  hal::KeyboardBBQ10 keyboard;
  hal::Trackball trackball;
#if defined(THINKNODE_M9)
  hal::KeypadM9 m9_keys;
  uint32_t m9_lv_key = 0;
  bool m9_lv_pressed = false;
#endif
  SdTileFs tile_fs{sd};
  core::OfflineTileReader tiles{&tile_fs};
  ui::App app{store, actions, tiles};

  uint16_t buf1[320 * 24];
  uint32_t last_input_ms = 0;
  uint8_t pending_key = 0;
  bool dimmed = false;
#endif
  uint32_t last_ms = 0;

  void begin(mesh::MainBoard* board, BaseSerialInterface* ifaces,
             DisplayDriver* disp, SensorManager* sens, NodePrefs* np) {
    sensors = sens;
    prefs = np;

    power.begin();
    buzzer.begin();
    if (prefs) {
      buzzer.setQuiet(prefs->buzzer_quiet != 0);
    }
    sd.begin();
    gps_port.begin(38400);


    actions.begin(board, static_cast<MultiSerialInterface*>(ifaces), sensors, prefs, &sd);
    actions.startRadioPolicy(false);  // region from the stored radio settings

#if !defined(FARO_HEADLESS)
    display = static_cast<FaroDisplay*>(disp);
    if (display) {
      display->reinitSpi();
    }
    touch.begin();
    keyboard.begin();
    trackball.begin();

    lv_init();
    lv_display_t* d = lv_display_create(320, 240);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buf1, nullptr, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(d, this);
    lv_display_set_flush_cb(d, [](lv_display_t* disp_obj, const lv_area_t* a, uint8_t* px) {
      auto* self = static_cast<DeviceController*>(lv_display_get_user_data(disp_obj));
      int w = a->x2 - a->x1 + 1;
      int h = a->y2 - a->y1 + 1;
      if (self && self->display) {
        self->display->flush(a->x1, a->y1, w, h, reinterpret_cast<const uint16_t*>(px));
      }
      lv_display_flush_ready(disp_obj);
    });

    lv_indev_t* indev_touch = lv_indev_create();
    lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev_touch, this);
    lv_indev_set_read_cb(indev_touch, [](lv_indev_t* indev, lv_indev_data_t* data) {
      auto* self = static_cast<DeviceController*>(lv_indev_get_user_data(indev));
      int16_t tx = 0, ty = 0;
      if (self && self->touch.read(tx, ty)) {
        data->point.x = tx;
        data->point.y = ty;
        if (self->dimmed) {
          self->wakeDisplay();
          data->state = LV_INDEV_STATE_RELEASED;
        } else {
          self->wakeDisplay();
          data->state = LV_INDEV_STATE_PRESSED;
        }
      } else {
        data->point.x = tx;
        data->point.y = ty;
        data->state = LV_INDEV_STATE_RELEASED;
      }
    });

    app.begin();

    // Register Trackball as LVGL Encoder input device bound to app.inputGroup() (ADR-018)
    lv_indev_t* indev_enc = lv_indev_create();
    lv_indev_set_type(indev_enc, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_user_data(indev_enc, this);
    if (app.inputGroup()) {
      lv_indev_set_group(indev_enc, app.inputGroup());
    }
    lv_indev_set_read_cb(indev_enc, [](lv_indev_t* indev, lv_indev_data_t* data) {
      auto* self = static_cast<DeviceController*>(lv_indev_get_user_data(indev));
      if (!self) return;
      int16_t dx = 0, dy = 0;
      bool clicked = false, long_pressed = false;
      self->trackball.poll(dx, dy, clicked, long_pressed);
      data->enc_diff = int16_t(dy + dx);
      data->state = clicked ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      if (dx != 0 || dy != 0 || clicked || long_pressed) {
        self->wakeDisplay();
        if (long_pressed) self->app.showScreen(ui::ScreenId::Home);
      }
    });

    // Register BBQ10 Physical Keyboard as LVGL Keypad input device bound to app.inputGroup()
    lv_indev_t* indev_kb = lv_indev_create();
    lv_indev_set_type(indev_kb, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(indev_kb, this);
    if (app.inputGroup()) {
      lv_indev_set_group(indev_kb, app.inputGroup());
    }
    lv_indev_set_read_cb(indev_kb, [](lv_indev_t* indev, lv_indev_data_t* data) {
      auto* self = static_cast<DeviceController*>(lv_indev_get_user_data(indev));
      if (!self) return;
      uint8_t k = self->keyboard.readKey();
      if (k != 0) {
        self->wakeDisplay();
        self->app.onKeyPress(k);
      }
      // App::onKeyPress(k) already handles _compose_ta insertion, backspace, enter,
      // and navigation hotkeys. Do not also emit LV_INDEV_STATE_PRESSED to LVGL's
      // keypad group, otherwise lv_textarea inserts each pressed character twice.
      data->key = 0;
      data->state = LV_INDEV_STATE_RELEASED;
    });

#if defined(THINKNODE_M9)
    // ThinkNode M9: the keypad is the only input (no touch, no trackball). Page keys move the focus,
    // enter clicks (or sends while typing), home/back return to Home, menu opens Settings.
    m9_keys.begin();
    lv_indev_t* indev_m9 = lv_indev_create();
    lv_indev_set_type(indev_m9, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(indev_m9, this);
    if (app.inputGroup()) {
      lv_indev_set_group(indev_m9, app.inputGroup());
    }
    lv_indev_set_read_cb(indev_m9, [](lv_indev_t* indev, lv_indev_data_t* data) {
      auto* self = static_cast<DeviceController*>(lv_indev_get_user_data(indev));
      if (!self) return;
      data->key = self->m9_lv_key;
      data->state = LV_INDEV_STATE_RELEASED;
      if (self->m9_lv_pressed) {  // one read pressed, the next one released
        self->m9_lv_pressed = false;
        return;
      }
      const uint8_t k = self->m9_keys.readKey(millis());
      if (k == 0) return;
      self->wakeDisplay();
      uint32_t lv_key = 0;
      switch (k) {
        case 0x81: lv_key = LV_KEY_PREV; break;
        case 0x85: lv_key = LV_KEY_NEXT; break;
        case 0x0D:
          if (self->app.isTyping()) self->app.onKeyPress('\r');
          else lv_key = LV_KEY_ENTER;
          break;
        case 0x82: case 0x86: case 0x1B: self->app.onKeyPress(27); break;
        case 0x83: self->app.openSettings(); break;
        default: self->app.onKeyPress(k); break;
      }
      if (lv_key) {
        self->m9_lv_key = lv_key;
        self->m9_lv_pressed = true;
        data->key = lv_key;
        data->state = LV_INDEV_STATE_PRESSED;
      }
    });
#endif
    last_input_ms = millis();
    if (sd.isMounted() && sd.hasDiscoveredTiles()) {
      bool tile_ok = tiles.hasTile(sd.defaultTileZ(), sd.defaultTileX(), sd.defaultTileY());
      Serial.printf("[FARO-SD] Tile decode self-test (%d/%d/%d): ok=%d (heap=%u, psram=%u)\n",
                    sd.defaultTileZ(), sd.defaultTileX(), sd.defaultTileY(), int(tile_ok),
                    unsigned(ESP.getFreeHeap()), unsigned(ESP.getFreePsram()));
    }
#else
    (void)disp;
#endif
    last_ms = millis();
  }


#if !defined(FARO_HEADLESS)
  void wakeDisplay() {
    last_input_ms = millis();
    if (dimmed && display) {
      uint8_t bright = store.uiPrefs().brightness_pct >= 80 ? store.uiPrefs().brightness_pct : 100;
      display->setBacklight(bright);
      dimmed = false;
      if (lv_screen_active()) {
        lv_obj_invalidate(lv_screen_active());
      }
    }
  }
#endif

  void loop() {
    uint32_t t0 = millis();
#if !defined(FARO_HEADLESS)
    lv_tick_inc(t0 - last_ms);
#endif
    last_ms = t0;

    // Drain SPSC EventBus into AppStore
    while (bus.pop(pop_ev)) {
      store.applyEvent(pop_ev, t0);
      if (pop_ev.type == core::MeshEventType::DirectMessageRecv ||
                 pop_ev.type == core::MeshEventType::ChannelMessageRecv) {
#if !defined(FARO_HEADLESS)
        wakeDisplay();
#endif
        buzzer.play(hal::TonePattern::MessageChime);
      }
    }

    uint16_t mv = power.readBattMilliVolts();
    uint8_t pct = power.readBattPercent(mv);
    actions.pollTelemetry(t0, mv, pct, power.isChargingEstimated(),
                          sd.isMounted(), sd.usedMB(), sd.totalMB(),
                          gps_port.sentenceCount());
    store.tickMessageTimeouts(t0);
    for (uint32_t resend_id = 0; store.takeResendRequest(resend_id);) {
      actions.resendDirectMessage(resend_id);
    }
    buzzer.loop(t0);

#if !defined(FARO_HEADLESS)
    app.tick(t0);

    // Hardware fast-wake check when dimmed (Trackball click GPIO 0 or GT911 Touch INT GPIO 16)
    if (dimmed) {
      if (digitalRead(0) == LOW) {
        wakeDisplay();
      }
#if defined(LILYGO_TDECK)
      if (digitalRead(16) == LOW) {
        wakeDisplay();
      }
#endif
    }

    // Idle screen dim after screen_timeout_secs
    uint32_t timeout_ms = uint32_t(store.uiPrefs().screen_timeout_secs) * 1000U;
    if (!dimmed && timeout_ms > 0 && (t0 - last_input_ms > timeout_ms)) {
      if (display) display->setBacklight(25);
      dimmed = true;
    }

    lv_timer_handler();
#endif

    uint32_t dt = millis() - t0;
    store.status().last_loop_ms = uint16_t(dt > 0xFFFF ? 0xFFFF : dt);
    if (store.status().last_loop_ms > store.status().max_loop_ms) {
      store.status().max_loop_ms = store.status().last_loop_ms;
    }
  }
};

}  // namespace faro

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  Serial.printf("[FARO] UITask::begin start (heap=%u, psram=%u)\n",
                unsigned(ESP.getFreeHeap()), unsigned(ESP.getFreePsram()));
#if !defined(FARO_HEADLESS)
  if (display) {
    static_cast<FaroDisplay*>(display)->reinitSpi();
    display->startFrame();
    display->setTextSize(2);
    display->drawTextCentered(display->width() / 2, 60, "Starting Faro UI...");
    display->endFrame();
  }
#endif
  if (!_ctrl) {
    _ctrl = new faro::DeviceController();
  }
  _ctrl->begin(_board, _interfaceManager, display, sensors, node_prefs);
#if !defined(FARO_HEADLESS)
  lv_tick_inc(20);
  lv_refr_now(nullptr);
  lv_timer_handler();
#endif
  Serial.printf("[FARO] UITask::begin ready (heap=%u, psram=%u)\n",
                unsigned(ESP.getFreeHeap()), unsigned(ESP.getFreePsram()));
}

void UITask::notify(UIEventType) {
  if (!_ctrl) return;
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::NotifyStateChanged;
  _ctrl->bus.push(ev);
}

void UITask::loop() {
  if (_ctrl) _ctrl->loop();
}

void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                           uint32_t sender_timestamp, const char* text) {
  if (!_ctrl || !text) return;


  // Never log message content (privacy): metadata only
  Serial.printf("[FARO-RX] DM (%uh, %u B)\n", unsigned(pkt ? pkt->path_len : 0), unsigned(strlen(text)));
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::DirectMessageRecv;
  ev.timestamp = sender_timestamp;
  ev.txt_type = txt_type;
  ev.snr_q4 = pkt ? int8_t(pkt->_snr) : 0;
  ev.rssi = int16_t(radio_driver.getLastRSSI());
  ev.hops = pkt ? pkt->path_len : 0;
  memcpy(ev.pub_key, from.id.pub_key, 32);
  strncpy(ev.name, from.name, sizeof(ev.name) - 1);
  ev.role = static_cast<faro::core::NodeRole>(from.type);
  strncpy(ev.text, text, sizeof(ev.text) - 1);
  _ctrl->bus.push(ev);
}

void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details,
                                  const char* text) {
  if (!_ctrl || !text) return;
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::ChannelMessageRecv;
  ev.timestamp = the_mesh.getRTCClock()->getCurrentTime();
  ev.snr_q4 = pkt ? int8_t(pkt->_snr) : 0;
  ev.rssi = int16_t(radio_driver.getLastRSSI());
  ev.hops = pkt ? pkt->path_len : 0;
  int idx = the_mesh.findChannelIdx(channel_details.channel);
  ev.channel_index = idx >= 0 ? uint8_t(idx) : 0;
  strncpy(ev.name, channel_details.name[0] ? channel_details.name : "Public", sizeof(ev.name) - 1);
  strncpy(ev.text, text, sizeof(ev.text) - 1);
  // Never log message content (privacy): metadata only
  Serial.printf("[FARO-RX] Channel[%u] (%uh, snr=%d, %u B)\n",
                unsigned(ev.channel_index), unsigned(ev.hops), int(ev.snr_q4), unsigned(strlen(ev.text)));
  _ctrl->bus.push(ev);
}

void UITask::onQueueSizeChanged(int msgcount) {
  if (!_ctrl) return;
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::QueueSizeChanged;
  ev.queue_size = uint16_t(msgcount >= 0 ? msgcount : 0);
  _ctrl->bus.push(ev);
}

void UITask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len,
                                 const uint8_t* path) {
  if (!_ctrl) return;
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::ContactDiscovered;
  ev.timestamp = contact.last_advert_timestamp;
  ev.is_new_contact = is_new;
  memcpy(ev.pub_key, contact.id.pub_key, 32);
  strncpy(ev.name, contact.name, sizeof(ev.name) - 1);
  ev.role = static_cast<faro::core::NodeRole>(contact.type);
  ev.lat_e6 = contact.gps_lat;
  ev.lon_e6 = contact.gps_lon;
  ev.path_len = path_len;
  if (path && path_len < 64) {
    memcpy(ev.path, path, path_len);
  }
  _ctrl->bus.push(ev);
}

void UITask::onACKRecv(uint32_t ack_crc) {
  if (!_ctrl) return;
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::AckRecv;
  ev.ack_crc = ack_crc;
  _ctrl->bus.push(ev);
}

void UITask::onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  if (!_ctrl) return;
  // path_len counts hash bytes; each hop has 1 << (flags & 3) of them and one SNR (x4)
  const uint8_t path_sz = flags & 0x03;
  const uint8_t hops = uint8_t(path_len >> path_sz);
  faro::core::MeshEvent ev{};
  ev.type = faro::core::MeshEventType::TraceRecv;
  ev.trace.tag = tag;
  ev.trace.auth_code = auth_code;
  ev.trace.flags = flags;
  ev.trace.hop_count = hops > 16 ? 16 : hops;
  for (uint8_t i = 0; i < ev.trace.hop_count; ++i) {
    ev.trace.hops[i].hash = path_hashes ? path_hashes[size_t(i) << path_sz] : 0;
    ev.trace.hops[i].snr_q4 = path_snrs ? int8_t(path_snrs[i]) : 0;
  }
  ev.trace.final_snr_q4 = pkt ? int8_t(pkt->getSNR() * 4) : 0;  // last hop, back to this device
  ev.snr_q4 = ev.trace.final_snr_q4;
  ev.rssi = int16_t(radio_driver.getLastRSSI());
  ev.trace.timestamp = the_mesh.getRTCClock()->getCurrentTime();
  ev.trace.valid = true;
  _ctrl->bus.push(ev);
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
  if (_ctrl) _ctrl->actions.onControlData(pkt);
}

void UITask::onChannelDataRecv(mesh::Packet*, const mesh::GroupChannel&, uint16_t data_type,
                               const uint8_t* data, size_t data_len) {
  (void)data_type;
  (void)data;
  (void)data_len;
}

// Answers to requests Faro sent itself (status, telemetry, neighbours, ACL, login, path discovery).
// Returning true keeps them from the phone app, which never asked for them.
bool UITask::onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) {
  return _ctrl ? _ctrl->actions.claimResponse(from, tag, data, len) : false;
}

void UITask::onRawDataRecv(mesh::Packet*) {}
#endif
