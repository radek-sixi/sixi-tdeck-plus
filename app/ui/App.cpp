// SPDX-License-Identifier: Apache-2.0
// app/ui/App.cpp — Touch-first 320x240 IPS LVGL 9 application shell & 15 screen views (ADR-018)
#include "App.h"
#include "Theme.h"
#include "fonts/FaroFonts.h"
#include "i18n/Strings.h"
#include "core/GeoMath.h"
#include "core/Random.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace faro::ui {

using namespace faro::core;
using namespace faro::ui::i18n;

namespace {

const char* roleBadge(NodeRole r) {
  switch (r) {
    case NodeRole::Repeater:   return "RPT";
    case NodeRole::RoomServer: return "ROOM";
    case NodeRole::Sensor:     return "SENS";
    case NodeRole::ChatNode:
    default:                   return "CHAT";
  }
}

const char* deliveryBadge(DeliveryState s) {
  switch (s) {
    case DeliveryState::Queued:       return "...";
    case DeliveryState::SentDirect:   return "DIR>";
    case DeliveryState::SentFlood:    return "FLD~";
    case DeliveryState::DeliveredAck: return "ACK OK";
    case DeliveryState::Failed:       return "ERR!";
    default:                          return "";
  }
}

void makeInitials(const char* name, char out[3]) {
  out[0] = 'N';
  out[1] = '\0';
  out[2] = '\0';
  if (!name || !name[0]) return;
  out[0] = name[0];
  const char* sp = strchr(name, ' ');
  if (!sp) sp = strchr(name, '-');
  if (!sp) sp = strchr(name, '_');
  if (sp && sp[1]) {
    out[1] = sp[1];
  } else if (name[1]) {
    out[1] = name[1];
  }
}

lv_obj_t* createAvatarCircle(lv_obj_t* parent, const char* name, uint8_t key_byte) {
  lv_obj_t* av = lv_obj_create(parent);
  lv_obj_set_size(av, 26, 26);
  lv_obj_set_style_radius(av, 13, 0);
  lv_obj_set_style_bg_color(av, Theme::avatarColor(key_byte), 0);
  lv_obj_set_style_bg_opa(av, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(av, 0, 0);
  lv_obj_set_style_pad_all(av, 0, 0);
  lv_obj_remove_flag(av, LV_OBJ_FLAG_SCROLLABLE);

  char ini[3] = {0};
  makeInitials(name, ini);
  lv_obj_t* lbl = lv_label_create(av);
  lv_obj_set_style_text_font(lbl, &faro_font_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(lbl, ini);
  lv_obj_center(lbl);
  return av;
}

lv_obj_t* createHeaderRow(lv_obj_t* parent, const char* title, bool outdoor,
                          lv_event_cb_t back_cb, void* user_data) {
  const auto& p = Theme::palette(outdoor);
  lv_obj_t* hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, 308, 32);
  lv_obj_set_style_bg_color(hdr, p.surface_alt, 0);
  lv_obj_set_style_border_color(hdr, p.border, 0);
  lv_obj_set_style_border_width(hdr, 1, 0);
  lv_obj_set_style_radius(hdr, 8, 0);
  lv_obj_set_style_pad_hor(hdr, 4, 0);
  lv_obj_set_style_pad_ver(hdr, 2, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  if (back_cb) {
    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 58, 26);
    Theme::styleTouchButton(back_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t* bl = lv_label_create(back_btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "< Back");
    lv_obj_center(bl);
  }

  lv_obj_t* t_lbl = lv_label_create(hdr);
  lv_obj_set_width(t_lbl, 168);
  lv_obj_set_style_text_font(t_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(t_lbl, p.text_primary, 0);
  lv_label_set_text(t_lbl, title);
  lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_CLIP);
  return hdr;
}

// Next TX power step at or below the region's ceiling (wraps to the lowest)
int8_t nextTxPower(int8_t cur, int8_t cap) {
  static const int8_t kSteps[] = {2, 5, 9, 12, 14, 17, 20, 22};
  for (int8_t s : kSteps) {
    if (s > cur && s <= cap) return s;
  }
  if (cur < cap) return cap;
  for (int8_t s : kSteps) {
    if (s <= cap) return s;
  }
  return cap;
}

}  // namespace

App::App(AppStore& store, IMeshActions& actions, OfflineTileReader& tile_reader)
  : _store(store), _actions(actions), _tiles(tile_reader) {}

const char* App::screenSlug(ScreenId id) {
  switch (id) {
    case ScreenId::Home:          return "01_home";
    case ScreenId::Chats:         return "02_chats";
    case ScreenId::ChatThread:    return "03_chat_thread";
    case ScreenId::Channels:      return "04_channels";
    case ScreenId::ChannelThread: return "05_channel_thread";
    case ScreenId::Discover:      return "06_discover";
    case ScreenId::Contacts:      return "07_contacts";
    case ScreenId::ContactDetail: return "08_contact_detail";
    case ScreenId::TracePath:     return "09_trace_path";
    case ScreenId::Map:           return "10_map";
    case ScreenId::Settings:      return "13_settings";
    case ScreenId::Diagnostics:   return "14_diagnostics";
    case ScreenId::BootWizard:    return "15_boot_wizard";
    default:                      return "unknown";
  }
}

void App::begin() {
  if (!_input_group) {
    _input_group = lv_group_create();
    lv_group_set_default(_input_group);
  }
  _last_bot_msg_id = (_store.messageCount() > 0)
                         ? _store.messageAt(_store.messageCount() - 1).id
                         : 0;
  _root = lv_screen_active();
  rebuildChrome();
  showScreen(_store.uiPrefs().first_boot_done ? ScreenId::Home : ScreenId::BootWizard);
}

namespace {
bool containsKeywordIgnoreCase(const char* haystack, const char* needle) {
  if (!haystack || !needle || !needle[0]) return false;
  size_t nlen = strlen(needle);
  for (const char* p = haystack; *p; ++p) {
    bool match = true;
    for (size_t i = 0; i < nlen; ++i) {
      char c1 = p[i];
      if (!c1) return false;
      if (c1 >= 'A' && c1 <= 'Z') c1 = char(c1 + 32);
      char c2 = needle[i];
      if (c2 >= 'A' && c2 <= 'Z') c2 = char(c2 + 32);
      if (c1 != c2) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void drawCanvasPixel(uint16_t* buf, int w, int h, int x, int y, uint16_t rgb565) {
  if (x >= 0 && x < w && y >= 0 && y < h) {
    buf[y * w + x] = rgb565;
  }
}

void drawCanvasLine(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t rgb565) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawCanvasPixel(buf, w, h, x0, y0, rgb565);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawCanvasCircle(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t rgb565) {
  for (int deg = 0; deg < 360; deg += 3) {
    double rad = double(deg) * 3.14159265358979323846 / 180.0;
    int x = cx + int(double(r) * cos(rad));
    int y = cy + int(double(r) * sin(rad));
    drawCanvasPixel(buf, w, h, x, y, rgb565);
  }
}

// Alarm texts with a position go only to a private (non-Public, non-hashtag) channel, never in clear on Public.
const ChannelRecord* firstPrivateChannel(const AppStore& store) {
  for (size_t ci = 0; ci < AppStore::kMaxChannels; ++ci) {
    const auto& ch = store.channels()[ci];
    if (ch.valid && ch.index != 0 && !ch.is_hashtag) return &ch;
  }
  return nullptr;
}
}  // namespace

void App::tick(uint32_t now_ms) {
  auto& up = _store.uiPrefs();

  // 1. PingBot / AwayBot Auto-Responder
  size_t mcnt = _store.messageCount();
  for (size_t i = 0; i < mcnt; ++i) {
    MessageRecord m = _store.messageAt(i);
    if (!m.valid || m.id <= _last_bot_msg_id) continue;
    _last_bot_msg_id = m.id;
    if (m.outgoing || up.bot_mode == 0) continue;
    if (strncmp(m.text, "[Faro Bot]", 10) == 0 ||
        strncmp(m.text, "[AWAY]", 6) == 0 ||
        strncmp(m.text, "[SOS", 4) == 0) {
      continue;
    }

    // Bots answer direct messages from known contacts only, at most once per 30 s.
    if (m.is_channel) continue;
    if (_last_bot_reply_ms != 0 && now_ms - _last_bot_reply_ms < 30000U) continue;

    if (up.bot_mode == 1) {
      // PingBot: reply to "ping", "test", "snr", "rssi"
      if (containsKeywordIgnoreCase(m.text, "ping") ||
          containsKeywordIgnoreCase(m.text, "test") ||
          containsKeywordIgnoreCase(m.text, "snr") ||
          containsKeywordIgnoreCase(m.text, "rssi")) {
        const auto& g = _store.gps();
        char pos[40] = {0};
        // Position only when location sharing is allowed; ~100 m precision, never a made-up fix
        if (_store.status().telemetry_loc_perm != 0 && g.valid) {
          snprintf(pos, sizeof(pos), " Pos:%.3f,%.3f", g.lat_deg, g.lon_deg);
        }
        char reply[148] = {0};
        snprintf(reply, sizeof(reply),
                 "[Faro Bot] Rx OK @%s! SNR:%+ddB RSSI:%ddBm Hops:%u%s",
                 m.sender_name[0] ? m.sender_name : "Peer",
                 int(m.snr_q4) / 4, int(m.rssi), unsigned(m.hops), pos);
        if (const auto* peer = _store.findContactByPrefix(m.peer_prefix)) {
          uint32_t crc = 0;
          DeliveryState dst = DeliveryState::Queued;
          _actions.sendDirectMessage(peer->pub_key, reply, crc, dst);
          up.bot_replies_sent++;
          _last_bot_reply_ms = now_ms ? now_ms : 1;
        }
        if (_store.messageCount() > 0) {
          _last_bot_msg_id = _store.messageAt(_store.messageCount() - 1).id;
        }
      }
    } else if (up.bot_mode == 2) {
      // AwayBot: auto-reply to incoming DMs
      if (const auto* peer = _store.findContactByPrefix(m.peer_prefix)) {
        char reply[148] = {0};
        snprintf(reply, sizeof(reply), "[AWAY] %s (Rx SNR:%+ddB)",
                 up.away_msg[0] ? up.away_msg : "Operator away from radio.",
                 int(m.snr_q4) / 4);
        uint32_t crc = 0;
        DeliveryState dst = DeliveryState::Queued;
        _actions.sendDirectMessage(peer->pub_key, reply, crc, dst);
        up.bot_replies_sent++;
        _last_bot_reply_ms = now_ms ? now_ms : 1;
        if (_store.messageCount() > 0) {
          _last_bot_msg_id = _store.messageAt(_store.messageCount() - 1).id;
        }
      }
    }
  }

  // 2. Lone-Worker / Dead-Man's Switch Countdown Timer
  if (up.deadman_mode != 0 && !up.deadman_triggered) {
    if (_last_deadman_tick_ms == 0) {
      _last_deadman_tick_ms = now_ms;
    } else if (now_ms > _last_deadman_tick_ms && (now_ms - _last_deadman_tick_ms) >= 1000) {
      uint32_t elapsed_s = (now_ms - _last_deadman_tick_ms) / 1000;
      _last_deadman_tick_ms += elapsed_s * 1000;
      if (up.deadman_remaining_secs > elapsed_s) {
        up.deadman_remaining_secs -= elapsed_s;
      } else {
        up.deadman_remaining_secs = 0;
        up.deadman_triggered = true;
        const auto& g = _store.gps();
        char pos[64] = {0};
        if (g.valid) {
          int32_t lv_e = 0, lv_n = 0;
          geomath::wgs84ToSwissLV95(g.lat_deg, g.lon_deg, lv_e, lv_n);
          snprintf(pos, sizeof(pos), "WGS:%.4f,%.4f LV95:%d,%d",
                   g.lat_deg, g.lon_deg, int(lv_e), int(lv_n));
        } else {
          snprintf(pos, sizeof(pos), "no GPS fix");  // never send a made-up position
        }
        char sos_msg[156] = {0};
        snprintf(sos_msg, sizeof(sos_msg),
                 "[SOS DEADMAN] %s missed check-in! %s Batt:%u%%",
                 _store.status().node_name, pos, unsigned(_store.status().batt_pct));
        const ChannelRecord* target = firstPrivateChannel(_store);
        if (!target || !_actions.sendChannelMessage(target->index, sos_msg)) {
          strncpy(_store.status().last_cli_reply, "Dead-man: no private channel configured - not sent.",
                  sizeof(_store.status().last_cli_reply) - 1);
        }
        _store.bumpVersion();
      }
    }
  } else {
    _last_deadman_tick_ms = now_ms;
  }

  if (_store.version() != _last_rendered_version) {
    updateStatusBar();
    renderBody();
    _last_rendered_version = _store.version();
  }
}

void App::showScreen(ScreenId id) {
  _current_screen = id;
  _home_subpage = false;
  _chats_subpage = false;
  _chat_thread_subpage = false;
  _channels_subpage = false;
  _channel_thread_subpage = false;
  _discover_subpage = false;
  _contacts_subpage = false;
  if (_input_group) {
    lv_group_remove_all_objs(_input_group);
  }
  rebuildChrome();
  updateStatusBar();
  renderBody();
  _last_rendered_version = _store.version();
}

void App::openChatWithContact(size_t contact_idx) {
  _selected_contact_idx = contact_idx;
  if (contact_idx < AppStore::kMaxContacts && _store.contacts()[contact_idx].valid) {
    const ContactRecord& c = _store.contacts()[contact_idx];
    _store.clearContactUnread(c.pub_key);
    // Battery / temperature of the peer when a direct path is known; never floods the mesh just
    // because a chat was opened
    if (c.hasPath()) _actions.requestTelemetry(c.pub_key);
  }
  showScreen(ScreenId::ChatThread);
}

void App::openChannelThread(uint8_t channel_index) {
  _selected_channel_idx = channel_index;
  _store.clearChannelUnread(channel_index);
  showScreen(ScreenId::ChannelThread);
}

void App::openContactDetail(size_t contact_idx) {
  _selected_contact_idx = contact_idx;
  showScreen(ScreenId::ContactDetail);
}

void App::onKeyPress(uint8_t key) {
  if (key == 27) {  // ESC -> unlock / back to Home
    _screen_locked = false;
    _show_my_qr_modal = false;
    _show_calc_modal = false;
    _show_compose_helper = false;
    showScreen(ScreenId::Home);
    return;
  }
  if (_compose_ta != nullptr) {
    if (key == '\b' || key == 127) {
      lv_textarea_delete_char(_compose_ta);
      return;
    }
    if (key == '\r' || key == '\n') {
      const char* cur = lv_textarea_get_text(_compose_ta);
      char sent[sizeof(_draft_text)] = {0};
      if (cur) strncpy(sent, cur, sizeof(sent) - 1);
      const char* txt = sent;
      if (txt[0]) {
        lv_textarea_set_text(_compose_ta, "");  // consumed: must not come back as a draft
        if (_screen_locked && _current_screen == ScreenId::Home) {
          // Quick Reply directly from Lock Screen (to last incoming DM or Public channel)
          bool sent_dm = false;
          for (size_t i = _store.messageCount(); i > 0; --i) {
            const auto& m = _store.messageAt(i - 1);
            if (m.valid && !m.outgoing) {
              if (!m.is_channel) {
                if (const auto* peer = _store.findContactByPrefix(m.peer_prefix)) {
                  uint32_t crc = 0;
                  DeliveryState st = DeliveryState::Queued;
                  _actions.sendDirectMessage(peer->pub_key, txt, crc, st);
                  sent_dm = true;
                }
              } else {
                _actions.sendChannelMessage(m.channel_index, txt);
                sent_dm = true;
              }
              break;
            }
          }
          if (!sent_dm) {
            _actions.sendChannelMessage(0, txt);
          }
          renderBody();
        } else if (_current_screen == ScreenId::Home && _home_subpage) {
          _actions.setNodeName(txt);
          _home_subpage = false;
          updateStatusBar();
          renderBody();
        } else if (_current_screen == ScreenId::Chats && _chats_subpage) {
          strncpy(_store.uiPrefs().search_query, txt, sizeof(_store.uiPrefs().search_query) - 1);
          _store.uiPrefs().search_active = (_store.uiPrefs().search_query[0] != '\0');
          _chats_subpage = false;
          renderBody();
        } else if (_current_screen == ScreenId::ChatThread) {
          const auto& c = _store.contacts()[_selected_contact_idx];
          uint32_t crc = 0;
          DeliveryState st = DeliveryState::Queued;
          _actions.sendDirectMessage(c.pub_key, txt, crc, st);
          renderBody();
        } else if (_current_screen == ScreenId::Channels && _channels_subpage) {
          _actions.addHashtagChannel(txt);
          _channels_subpage = false;
          renderBody();
        } else if (_current_screen == ScreenId::ChannelThread) {
          if (_channel_thread_subpage) {
            _actions.addHashtagChannel(txt);
            _channel_thread_subpage = false;
          } else {
            _actions.sendChannelMessage(_selected_channel_idx, txt);
          }
          renderBody();
        } else if (_current_screen == ScreenId::ContactDetail && _contact_detail_tab == 3) {
          const auto& c = _store.contacts()[_selected_contact_idx];
          _actions.sendRemoteCli(c.pub_key, txt);
          renderBody();
        } else if (_current_screen == ScreenId::Settings && !_settings_hub) {
          if (_settings_tab == 1) {
            _actions.setNodeName(txt);
            updateStatusBar();
            renderBody();
          } else if (_settings_tab == 4) {
            if (_wifi_edit_pwd) {
              _actions.setWifiConfig(true, _store.status().wifi_ssid, txt);
            } else {
              _actions.setWifiConfig(true, txt, _store.status().wifi_pwd);
            }
            updateStatusBar();
            renderBody();
          } else if (_settings_tab == 5) {
            uint32_t parsed = uint32_t(atoi(txt));
            if (parsed >= 100000 && parsed <= 999999) {
              _actions.setBlePin(parsed);
              renderBody();
            }
          } else if (_settings_tab == 7) {
            char reply[256] = {0};
            _actions.runCliCommand(txt, reply, sizeof(reply));
            renderBody();
          }
        }
      }
      return;
    }
    if (key >= 32 && key < 127) {
      lv_textarea_add_char(_compose_ta, char(key));
      return;
    }
  }

  // Ripple UI shortcuts when not typing in a text box:
  // Space toggles Home <-> Map (or toggles Age/Hash column in Discover)
  if (key == ' ') {
    if (_current_screen == ScreenId::Discover) {
      _discover_show_hash = !_discover_show_hash;
      renderBody();
      return;
    } else if (_current_screen == ScreenId::Home || _current_screen == ScreenId::Chats) {
      showScreen(ScreenId::Map);
      return;
    } else if (_current_screen == ScreenId::Map) {
      showScreen(ScreenId::Chats);
      return;
    }
  }
  // ',' and '.' cycle contact filters in Chats / Contacts / Discover
  if (key == '.' || key == ',') {
    if (_current_screen == ScreenId::Chats || _current_screen == ScreenId::Contacts) {
      _contact_filter = uint8_t((_contact_filter + (key == '.' ? 1 : 6)) % 7);
      renderBody();
      return;
    } else if (_current_screen == ScreenId::Discover) {
      _discover_filter = uint8_t((_discover_filter + (key == '.' ? 1 : 4)) % 5);
      renderBody();
      return;
    }
  }
  // A-Z jump in Contacts / Chats / Discover
  if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
    char target_upper = (key >= 'a' && key <= 'z') ? char(key - 32) : char(key);
    for (size_t step = 1; step <= AppStore::kMaxContacts; ++step) {
      size_t idx = (_selected_contact_idx + step) % AppStore::kMaxContacts;
      const auto& c = _store.contacts()[idx];
      if (!c.valid || !c.name[0]) continue;
      char c0 = (c.name[0] >= 'a' && c.name[0] <= 'z') ? char(c.name[0] - 32) : c.name[0];
      if (c0 == target_upper) {
        _selected_contact_idx = idx;
        renderBody();
        return;
      }
    }
  }

  if (_input_group != nullptr) {
    if (key == '\t' || key == ']') {
      lv_group_focus_next(_input_group);
    } else if (key == '[') {
      lv_group_focus_prev(_input_group);
    }
  }
}

void App::rebuildChrome() {
  if (!_root) _root = lv_screen_active();
  lv_obj_clean(_root);
  _compose_ta = nullptr;

  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  lv_obj_set_style_bg_color(_root, p.bg, 0);
  lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Persistent TopStatusBar (320x22) — tappable to return Home from any screen
  _top_bar = lv_obj_create(_root);
  lv_obj_set_size(_top_bar, 320, 22);
  lv_obj_set_pos(_top_bar, 0, 0);
  lv_obj_set_style_bg_color(_top_bar, p.surface, 0);
  lv_obj_set_style_bg_opa(_top_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_top_bar, p.border, 0);
  lv_obj_set_style_border_width(_top_bar, 1, 0);
  lv_obj_set_style_border_side(_top_bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(_top_bar, 0, 0);
  lv_obj_set_style_pad_hor(_top_bar, 6, 0);
  lv_obj_set_style_pad_ver(_top_bar, 2, 0);
  lv_obj_remove_flag(_top_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_top_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_top_bar, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self && self->_current_screen != ScreenId::Home) {
      self->showScreen(ScreenId::Home);
    }
  }, LV_EVENT_CLICKED, this);

  _top_left_lbl = lv_label_create(_top_bar);
  lv_obj_set_style_text_font(_top_left_lbl, &faro_font_12, 0);
  lv_obj_set_style_text_color(_top_left_lbl, p.accent, 0);
  lv_obj_align(_top_left_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  _top_right_lbl = lv_label_create(_top_bar);
  lv_obj_set_style_text_font(_top_right_lbl, &faro_font_12, 0);
  lv_obj_set_style_text_color(_top_right_lbl, p.text_primary, 0);
  lv_obj_align(_top_right_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

  bool hide_bottom_nav = (_current_screen == ScreenId::ChatThread ||
                          _current_screen == ScreenId::ChannelThread ||
                          _current_screen == ScreenId::BootWizard ||
                          _screen_locked ||
                          (_current_screen == ScreenId::Settings && !_settings_hub));

  // 2. Content Body (320x178 when bottom nav visible, 320x218 in full-screen thread/subpage views)
  _body = lv_obj_create(_root);
  lv_obj_set_size(_body, 320, hide_bottom_nav ? 218 : 178);
  lv_obj_set_pos(_body, 0, 22);
  lv_obj_set_style_bg_color(_body, p.bg, 0);
  lv_obj_set_style_bg_opa(_body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_body, 0, 0);
  lv_obj_set_style_radius(_body, 0, 0);
  lv_obj_set_style_pad_all(_body, 6, 0);
  lv_obj_set_style_pad_row(_body, 5, 0);
  lv_obj_set_flex_flow(_body, LV_FLEX_FLOW_COLUMN);

  // 3. Persistent BottomTabBar (320x40 at y=200, 5 targets of 61x34 px: Home | Chats | Channels | Map | Settings)
  _bottom_bar = lv_obj_create(_root);
  lv_obj_set_size(_bottom_bar, 320, 40);
  lv_obj_set_pos(_bottom_bar, 0, 200);
  lv_obj_set_style_bg_color(_bottom_bar, p.surface, 0);
  lv_obj_set_style_bg_opa(_bottom_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_bottom_bar, p.border, 0);
  lv_obj_set_style_border_width(_bottom_bar, 1, 0);
  lv_obj_set_style_radius(_bottom_bar, 0, 0);
  lv_obj_set_style_pad_all(_bottom_bar, 2, 0);
  lv_obj_set_style_pad_column(_bottom_bar, 2, 0);
  lv_obj_set_flex_flow(_bottom_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
  if (hide_bottom_nav) {
    lv_obj_add_flag(_bottom_bar, LV_OBJ_FLAG_HIDDEN);
  }

  struct TabSpec {
    ScreenId target;
    StrId    label_id;
  };
  static const TabSpec kTabs[5] = {
    {ScreenId::Home,     StrId::HomeDashboard},
    {ScreenId::Chats,    StrId::TabChats},
    {ScreenId::Channels, StrId::TabChannels},
    {ScreenId::Map,      StrId::TabMap},
    {ScreenId::Settings, StrId::TabSettings}
  };

  for (int i = 0; i < 5; ++i) {
    lv_obj_t* btn = lv_button_create(_bottom_bar);
    lv_obj_set_size(btn, 61, 34);
    bool active = (_current_screen == kTabs[i].target) ||
                  (i == 1 && _current_screen == ScreenId::ChatThread) ||
                  (i == 2 && _current_screen == ScreenId::ChannelThread);
    Theme::styleTouchButton(btn, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    struct TabCtx { App* app; ScreenId scr; };
    static TabCtx s_tab_ctx[5];
    s_tab_ctx[i] = {this, kTabs[i].target};
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      auto* ctx = static_cast<TabCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        if (ctx->scr == ScreenId::Settings) {
          ctx->app->_settings_hub = true;
        }
        ctx->app->showScreen(ctx->scr);
      }
    }, LV_EVENT_CLICKED, &s_tab_ctx[i]);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &faro_font_12, 0);
    if (i == 0) {
      lv_label_set_text(lbl, "Home");
    } else {
      uint32_t unread = (i == 1) ? _store.totalUnreadDm() : ((i == 2) ? _store.totalUnreadChannels() : 0);
      if (unread > 0) {
        lv_label_set_text_fmt(lbl, "%s(%u)", tr(kTabs[i].label_id, lang), unsigned(unread));
      } else {
        lv_label_set_text(lbl, tr(kTabs[i].label_id, lang));
      }
    }
    lv_obj_center(lbl);
  }
}

void App::updateStatusBar() {
  if (!_top_left_lbl || !_top_right_lbl) return;
  const auto& st = _store.status();
  const auto& g = _store.gps();
  const auto& rr = _store.repeatRoam();

  const auto& up = _store.uiPrefs();
  // Region, then the strongest repeater heard lately (shown only: MeshCore routes as always)
  char left_buf[96] = {0};
  snprintf(left_buf, sizeof(left_buf), "%.8s%s%s|%.9s|%s%.6s",
           st.node_name,
           up.bot_mode == 1 ? "[PING]" : (up.bot_mode == 2 ? "[AWAY]" : ""),
           up.deadman_mode ? "[DM]" : "",
           _store.regionInfo().code,
           rr.hasPreferred() ? "R:" : "",
           rr.hasPreferred() ? rr.preferredName() : "");
  lv_label_set_text(_top_left_lbl, left_buf);

  char right_buf[96] = {0};
  int snr_int = int(st.last_snr_q4) / 4;
  snprintf(right_buf, sizeof(right_buf), "%s%s %s %ddB %u%%",
           st.ble_connected ? "BLE " : "",
           st.wifi_connected ? "WiFi" : "",
           g.valid ? "GPS" : "--",
           snr_int,
           unsigned(st.batt_pct));
  lv_label_set_text(_top_right_lbl, right_buf);
}

bool App::confirmTap(int action_id, const char* warning) {
  const uint32_t now = lv_tick_get();
  if (_armed_action == action_id && now - _armed_ms <= 5000U) {
    _armed_action = -1;
    return true;
  }
  _armed_action = action_id;
  _armed_ms = now;
  strncpy(_store.status().sd_backup_status, warning, sizeof(_store.status().sd_backup_status) - 1);
  _store.status().sd_backup_status[sizeof(_store.status().sd_backup_status) - 1] = '\0';
  return false;
}

uint32_t App::draftKey() const {
  return (uint32_t(_current_screen) << 24) ^ (uint32_t(_selected_contact_idx & 0xFFF) << 12) ^
         uint32_t(_selected_channel_idx & 0xFFF);
}

void App::renderBody() {
  if (!_body) return;
  if (_compose_ta) {
    const char* cur = lv_textarea_get_text(_compose_ta);
    strncpy(_draft_text, cur ? cur : "", sizeof(_draft_text) - 1);
    _draft_text[sizeof(_draft_text) - 1] = '\0';
    _draft_key = _compose_key;
    _draft_focused = _input_group && lv_group_get_focused(_input_group) == _compose_ta;
  }
  lv_obj_clean(_body);
  _compose_ta = nullptr;

  switch (_current_screen) {
    case ScreenId::Home:          buildHomeView(_body); break;
    case ScreenId::Chats:         buildChatsView(_body); break;
    case ScreenId::ChatThread:    buildChatThreadView(_body); break;
    case ScreenId::Channels:      buildChannelsView(_body); break;
    case ScreenId::ChannelThread: buildChannelThreadView(_body); break;
    case ScreenId::Discover:      buildDiscoverView(_body); break;
    case ScreenId::Contacts:      buildContactsView(_body); break;
    case ScreenId::ContactDetail: buildContactDetailView(_body); break;
    case ScreenId::TracePath:     buildTracePathView(_body); break;
    case ScreenId::Map:           buildMapView(_body); break;
    case ScreenId::Settings:      buildSettingsView(_body); break;
    case ScreenId::Diagnostics:   buildDiagnosticsView(_body); break;
    case ScreenId::BootWizard:    buildBootWizardView(_body); break;
    default:                      buildHomeView(_body); break;
  }

  if (_compose_ta) {
    if (_draft_key == draftKey() && _draft_text[0]) {
      lv_textarea_set_text(_compose_ta, _draft_text);
    }
    if (_draft_key == draftKey() && _draft_focused && _input_group) {
      lv_group_focus_obj(_compose_ta);
    }
    _compose_key = draftKey();
    const auto& p = Theme::palette(_store.uiPrefs().outdoor_high_contrast);
    lv_obj_set_style_bg_color(_compose_ta, p.surface, 0);
    lv_obj_set_style_bg_opa(_compose_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(_compose_ta, p.text_primary, 0);
    lv_obj_set_style_text_color(_compose_ta, p.text_secondary, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(_compose_ta, p.border, 0);
    lv_obj_set_style_border_width(_compose_ta, 1, 0);
    lv_obj_set_style_radius(_compose_ta, 8, 0);
  }
}

void App::buildHomeView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);
  const auto& st = _store.status();
  const auto& r = _store.radio();

  // Pocket Lock Screen with Live RX Preview & Quick Reply (Enter)
  if (_screen_locked) {
    lv_obj_t* lcard = lv_obj_create(parent);
    lv_obj_set_size(lcard, 308, 206);
    Theme::styleCard(lcard, outdoor);
    lv_obj_set_style_border_color(lcard, p.accent, 0);
    lv_obj_set_style_border_width(lcard, 2, 0);
    lv_obj_remove_flag(lcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* h_lbl = lv_label_create(lcard);
    lv_obj_set_pos(h_lbl, 2, 0);
    lv_obj_set_style_text_font(h_lbl, &faro_font_14, 0);
    lv_obj_set_style_text_color(h_lbl, p.accent, 0);
    lv_label_set_text_fmt(h_lbl, "SCREEN LOCKED (RX ON) | %.3fMHz | %u%%",
                          double(r.freq_mhz), unsigned(st.batt_pct));

    // Preview of latest message
    const MessageRecord* last_msg = nullptr;
    for (size_t i = _store.messageCount(); i > 0; --i) {
      if (_store.messageAt(i - 1).valid) {
        last_msg = &_store.messageAt(i - 1);
        break;
      }
    }

    lv_obj_t* mbox = lv_obj_create(lcard);
    lv_obj_set_size(mbox, 294, 78);
    lv_obj_set_pos(mbox, 0, 24);
    lv_obj_set_style_bg_color(mbox, p.surface_alt, 0);
    lv_obj_set_style_border_color(mbox, p.border, 0);
    lv_obj_set_style_border_width(mbox, 1, 0);
    lv_obj_set_style_radius(mbox, 8, 0);
    lv_obj_set_style_pad_all(mbox, 6, 0);
    lv_obj_remove_flag(mbox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* mlbl = lv_label_create(mbox);
    lv_obj_set_width(mlbl, 280);
    lv_label_set_long_mode(mlbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(mlbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(mlbl, p.text_primary, 0);
    if (last_msg) {
      lv_label_set_text_fmt(mlbl, "%s (%s, %uh):\n%s",
                            last_msg->sender_name[0] ? last_msg->sender_name : "Peer",
                            last_msg->is_channel ? "Channel" : "DM",
                            unsigned(last_msg->hops),
                            last_msg->text);
    } else {
      lv_label_set_text(mlbl, "Radio listening in background.\nType & press Enter for Quick Reply.");
    }

    // Quick Reply bar directly on Lock Screen
    _compose_ta = lv_textarea_create(lcard);
    lv_obj_set_size(_compose_ta, 206, 32);
    lv_obj_set_pos(_compose_ta, 0, 110);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_placeholder_text(_compose_ta, "Quick reply (Enter)...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* qr_send = lv_button_create(lcard);
    lv_obj_set_size(qr_send, 82, 32);
    lv_obj_set_pos(qr_send, 212, 110);
    Theme::styleTouchButton(qr_send, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(qr_send, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self) self->onKeyPress('\r');
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* qsl = lv_label_create(qr_send);
    lv_obj_set_style_text_font(qsl, &faro_font_12, 0);
    lv_label_set_text(qsl, tr(StrId::BtnSend, lang));
    lv_obj_center(qsl);

    // Large Unlock button
    lv_obj_t* unl_btn = lv_button_create(lcard);
    lv_obj_set_size(unl_btn, 294, 38);
    lv_obj_set_pos(unl_btn, 0, 150);
    Theme::styleTouchButton(unl_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(unl_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self) return;
      self->_screen_locked = false;
      self->rebuildChrome();
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* ull = lv_label_create(unl_btn);
    lv_obj_set_style_text_font(ull, &faro_font_14, 0);
    lv_label_set_text(ull, "Unlock Screen (or ESC)");
    lv_obj_center(ull);
    return;
  }

  // Optional Self QR Modal overlay
  if (_show_my_qr_modal) {
    lv_obj_t* mcard = lv_obj_create(parent);
    lv_obj_set_size(mcard, 308, 166);
    Theme::styleCard(mcard, outdoor);
    lv_obj_remove_flag(mcard, LV_OBJ_FLAG_SCROLLABLE);

    ContactRecord self_c{};
    memcpy(self_c.pub_key, st.self_pubkey, 32);
    strncpy(self_c.name, st.node_name, sizeof(self_c.name) - 1);
    self_c.role = NodeRole::ChatNode;
    self_c.valid = true;

    char uri[220] = {0};
    if (AppStore::formatContactUri(self_c, uri, sizeof(uri))) {
      lv_obj_t* qr = lv_qrcode_create(mcard);
      lv_qrcode_set_size(qr, 108);
      lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
      lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
      lv_qrcode_update(qr, uri, uint32_t(strlen(uri)));
      lv_obj_set_pos(qr, 4, 4);
    }

    char pub_hex[20] = {0};
    AppStore::bytesToHex(st.self_pubkey, 8, pub_hex, sizeof(pub_hex));
    lv_obj_t* info = lv_label_create(mcard);
    lv_obj_set_pos(info, 120, 4);
    lv_obj_set_width(info, 174);
    lv_obj_set_style_text_font(info, &faro_font_12, 0);
    lv_obj_set_style_text_color(info, p.text_primary, 0);
    lv_label_set_text_fmt(info, "My Identity QR\nNode: %s\nKey: %s...\nScope: %s\n%.3f MHz",
                          st.node_name, pub_hex, st.active_scope_name, double(r.freq_mhz));

    lv_obj_t* cp_btn = lv_button_create(mcard);
    lv_obj_set_size(cp_btn, 84, 28);
    lv_obj_set_pos(cp_btn, 120, 118);
    Theme::styleTouchButton(cp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(cp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      ContactRecord sc{};
      memcpy(sc.pub_key, self->_store.status().self_pubkey, 32);
      strncpy(sc.name, self->_store.status().node_name, sizeof(sc.name) - 1);
      sc.valid = true;
      AppStore::formatContactUri(sc, self->_store.uiPrefs().clipboard, sizeof(self->_store.uiPrefs().clipboard));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cpl = lv_label_create(cp_btn);
    lv_obj_set_style_text_font(cpl, &faro_font_12, 0);
    lv_label_set_text(cpl, "Copy URI");
    lv_obj_center(cpl);

    lv_obj_t* cl_btn = lv_button_create(mcard);
    lv_obj_set_size(cl_btn, 84, 28);
    lv_obj_set_pos(cl_btn, 210, 118);
    Theme::styleTouchButton(cl_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(cl_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_show_my_qr_modal = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cll = lv_label_create(cl_btn);
    lv_obj_set_style_text_font(cll, &faro_font_12, 0);
    lv_label_set_text(cll, "Close");
    lv_obj_center(cll);
    return;
  }

  // Optional Built-in Calculator Modal (Ripple UI p.8)
  if (_show_calc_modal) {
    lv_obj_t* ccard = lv_obj_create(parent);
    lv_obj_set_size(ccard, 308, 166);
    Theme::styleCard(ccard, outdoor);
    lv_obj_remove_flag(ccard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* disp_lbl = lv_label_create(ccard);
    lv_obj_set_pos(disp_lbl, 4, 2);
    lv_obj_set_style_text_font(disp_lbl, &faro_font_16, 0);
    lv_obj_set_style_text_color(disp_lbl, p.accent, 0);
    lv_label_set_text_fmt(disp_lbl, "CALC: %d %c %d", int(_calc_acc), _calc_op ? _calc_op : ' ', int(_calc_cur));

    static const char* kKeys[16] = {
      "7", "8", "9", "/",
      "4", "5", "6", "*",
      "1", "2", "3", "-",
      "C", "0", "=", "+"
    };
    struct CalcCtx { App* app; char k; };
    static CalcCtx s_cctx[16];
    for (int i = 0; i < 16; ++i) {
      lv_obj_t* kb = lv_button_create(ccard);
      lv_obj_set_size(kb, 54, 26);
      lv_obj_set_pos(kb, (i % 4) * 58, 28 + (i / 4) * 30);
      Theme::styleTouchButton(kb, p.surface_alt, p.text_primary);
      s_cctx[i] = {this, kKeys[i][0]};
      lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        auto* ctx = static_cast<CalcCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        char k = ctx->k;
        if (k >= '0' && k <= '9') {
          a->_calc_cur = a->_calc_cur * 10 + (k - '0');
        } else if (k == 'C') {
          a->_calc_acc = 0; a->_calc_cur = 0; a->_calc_op = 0;
        } else if (k == '=') {
          if (a->_calc_op == '+') a->_calc_cur = a->_calc_acc + a->_calc_cur;
          else if (a->_calc_op == '-') a->_calc_cur = a->_calc_acc - a->_calc_cur;
          else if (a->_calc_op == '*') a->_calc_cur = a->_calc_acc * a->_calc_cur;
          else if (a->_calc_op == '/' && a->_calc_cur != 0) a->_calc_cur = a->_calc_acc / a->_calc_cur;
          a->_calc_acc = a->_calc_cur;
          a->_calc_op = 0;
        } else {
          a->_calc_acc = a->_calc_cur;
          a->_calc_cur = 0;
          a->_calc_op = k;
        }
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_cctx[i]);
      lv_obj_t* kl = lv_label_create(kb);
      lv_obj_set_style_text_font(kl, &faro_font_12, 0);
      lv_label_set_text(kl, kKeys[i]);
      lv_obj_center(kl);
    }

    lv_obj_t* x_btn = lv_button_create(ccard);
    lv_obj_set_size(x_btn, 56, 28);
    lv_obj_set_pos(x_btn, 236, 28);
    Theme::styleTouchButton(x_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(x_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_show_calc_modal = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* xl = lv_label_create(x_btn);
    lv_obj_set_style_text_font(xl, &faro_font_12, 0);
    lv_label_set_text(xl, "Close");
    lv_obj_center(xl);
    return;
  }

  // Ripple UI 'Kid Mode' (Simple 4-Tile Home Screen, Ripple p.20-21)
  if (_store.uiPrefs().kid_mode) {
    static const uint32_t kTileCols[4] = {0xE37400, 0x1E8E3E, 0x1A73E8, 0x9334E6};
    struct KidCtx { App* app; size_t idx; };
    static KidCtx s_kctx[4];
    size_t shown = 0;
    for (size_t i = 0; i < AppStore::kMaxContacts && shown < 4; ++i) {
      const auto& c = _store.contacts()[i];
      if (!c.valid) continue;
      lv_obj_t* tile = lv_button_create(parent);
      lv_obj_set_size(tile, 308, 36);
      Theme::styleTouchButton(tile, lv_color_hex(kTileCols[shown]), lv_color_hex(0xFFFFFF));
      s_kctx[shown] = {this, i};
      lv_obj_add_event_cb(tile, [](lv_event_t* e) {
        auto* ctx = static_cast<KidCtx*>(lv_event_get_user_data(e));
        ctx->app->openChatWithContact(ctx->idx);
      }, LV_EVENT_CLICKED, &s_kctx[shown]);
      lv_obj_t* tl = lv_label_create(tile);
      lv_obj_set_style_text_font(tl, &faro_font_16, 0);
      lv_label_set_text_fmt(tl, "%s %s", c.name, c.unread_count > 0 ? "(NEW!)" : "");
      lv_obj_center(tl);
      ++shown;
    }
    return;
  }


  // Dedicated Home Node Editor & Quick Actions Subpage (Node Name, Radio Setup, SD Import/Export, QR, Theme)
  if (_home_subpage) {
    createHeaderRow(parent, "Node & Quick Actions", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_home_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* hcard = lv_obj_create(parent);
    lv_obj_set_size(hcard, 308, 128);
    Theme::styleCard(hcard, outdoor);
    lv_obj_set_style_pad_all(hcard, 6, 0);
    lv_obj_remove_flag(hcard, LV_OBJ_FLAG_SCROLLABLE);

    _compose_ta = lv_textarea_create(hcard);
    lv_obj_set_size(_compose_ta, 194, 30);
    lv_obj_set_pos(_compose_ta, 0, 0);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, st.node_name);
    lv_textarea_set_placeholder_text(_compose_ta, "Type Node Name...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sn_btn = lv_button_create(hcard);
    lv_obj_set_size(sn_btn, 96, 30);
    lv_obj_set_pos(sn_btn, 198, 0);
    Theme::styleTouchButton(sn_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sn_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self && self->_compose_ta) {
        self->_actions.setNodeName(lv_textarea_get_text(self->_compose_ta));
        self->_home_subpage = false;
        self->updateStatusBar();
        self->renderBody();
      }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* snl = lv_label_create(sn_btn);
    lv_obj_set_style_text_font(snl, &faro_font_12, 0);
    lv_label_set_text(snl, "Save Name");
    lv_obj_center(snl);

    struct HSubCtx { App* app; int idx; };
    static HSubCtx s_hsctx[4];
    static const uint32_t kHSubCols[4] = {0x007B83, 0xE37400, 0x1A73E8, 0x1E8E3E};
    const char* kHSubLabels[4] = {
      "Node & Radio Setup",
      "SD Import / Export",
      "My Identity QR",
      outdoor ? "Theme: Light" : "Theme: Dark"
    };

    for (int i = 0; i < 4; ++i) {
      lv_obj_t* b = lv_button_create(hcard);
      lv_obj_set_size(b, 144, 36);
      lv_obj_set_pos(b, (i % 2) * 150, 36 + (i / 2) * 40);
      Theme::styleTouchButton(b, lv_color_hex(kHSubCols[i]), lv_color_hex(0xFFFFFF));
      s_hsctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<HSubCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        a->_home_subpage = false;
        if (ctx->idx == 0) {
          a->_settings_tab = 1;
          a->_settings_hub = false;
          a->_current_screen = ScreenId::Settings;
          a->rebuildChrome();
          a->updateStatusBar();
          a->renderBody();
        } else if (ctx->idx == 1) {
          a->_settings_tab = 8;
          a->_settings_hub = false;
          a->_current_screen = ScreenId::Settings;
          a->rebuildChrome();
          a->updateStatusBar();
          a->renderBody();
        } else if (ctx->idx == 2) {
          a->_show_my_qr_modal = true;
          a->renderBody();
        } else {
          a->_store.uiPrefs().outdoor_high_contrast = !a->_store.uiPrefs().outdoor_high_contrast;
          a->rebuildChrome();
          a->updateStatusBar();
          a->renderBody();
        }
      }, LV_EVENT_CLICKED, &s_hsctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, kHSubLabels[i]);
      lv_obj_center(l);
    }
    return;
  }

  // Node & Radio Summary Card (Tap card to edit Node Name, SD Backup, QR, or Theme)
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, 308, 52);
  Theme::styleCard(card, outdoor);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) {
      self->_home_subpage = true;
      self->renderBody();
    }
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* title = lv_label_create(card);
  lv_obj_set_style_text_font(title, &faro_font_14, 0);
  lv_obj_set_style_text_color(title, p.text_primary, 0);
  lv_label_set_text_fmt(title, "Node: %s (%s)  [Edit Node >]",
                        st.node_name,
                        languageCode(lang));
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t* sub = lv_label_create(card);
  lv_obj_set_style_text_font(sub, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(sub, p.text_secondary, 0);
  lv_label_set_text_fmt(sub, "%.3fMHz BW%.1fk SF%u %ddBm | N:%zu %u%%",
                        double(r.freq_mhz), double(r.bw_khz), unsigned(r.sf),
                        int(r.tx_power_dbm), _store.contactCount(),
                        unsigned(st.batt_pct));
  lv_obj_set_pos(sub, 0, 22);

  // Clean 2x2 Touch Grid (4 large 150x48 px tiles) for effortless finger navigation:
  // Row 1: [1. Contacts]      [2. Discover]
  // Row 2: [3. Alerts/Graph]  [4. SOS/Diagnostics]
  struct NavCtx { App* app; int idx; };
  static NavCtx s_qctx[4];
  static const uint32_t kTileCols[4] = {0x1A73E8, 0x1E8E3E, 0x9334E6, 0xE37400};

  for (int row_idx = 0; row_idx < 2; ++row_idx) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 48);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (int col_idx = 0; col_idx < 2; ++col_idx) {
      int i = row_idx * 2 + col_idx;
      lv_obj_t* b = lv_button_create(row);
      lv_obj_set_size(b, 150, 46);
      uint32_t col = kTileCols[i];
      Theme::styleTouchButton(b, lv_color_hex(col), lv_color_hex(0xFFFFFF));
      s_qctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* c = static_cast<NavCtx*>(lv_event_get_user_data(e));
        if (!c || !c->app) return;
        App* a = c->app;
        if (c->idx == 0) {
          a->showScreen(ScreenId::Contacts);
        } else if (c->idx == 1) {
          a->showScreen(ScreenId::Discover);
        } else if (c->idx == 2) {
          a->_diag_tab = 2;  // 2D Mesh Topology Graph
          a->showScreen(ScreenId::Diagnostics);
        } else {
          a->_diag_tab = 0;
          a->showScreen(ScreenId::Diagnostics);
        }
      }, LV_EVENT_CLICKED, &s_qctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_14, 0);
      if (i == 0) {
        lv_label_set_text_fmt(l, "%s (%zu)", tr(StrId::BtnContacts, lang), _store.contactCount());
      } else if (i == 1) {
        lv_label_set_text(l, tr(StrId::TabDiscover, lang));
      } else if (i == 2) {
        lv_label_set_text(l, "RF Mesh Graph");
      } else {
        lv_label_set_text(l, tr(StrId::BtnDiagnostics, lang));
      }
      lv_obj_center(l);
    }
  }
}

void App::buildChatsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  if (_chats_subpage) {
    createHeaderRow(parent, "Chats Tools", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_chats_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* scard = lv_obj_create(parent);
    lv_obj_set_size(scard, 308, 128);
    Theme::styleCard(scard, outdoor);
    lv_obj_set_style_pad_all(scard, 6, 0);
    lv_obj_remove_flag(scard, LV_OBJ_FLAG_SCROLLABLE);

    _compose_ta = lv_textarea_create(scard);
    lv_obj_set_size(_compose_ta, 206, 28);
    lv_obj_set_pos(_compose_ta, 0, 0);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, _store.uiPrefs().search_query);
    lv_textarea_set_placeholder_text(_compose_ta, "Filter contacts / notes...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* go_btn = lv_button_create(scard);
    lv_obj_set_size(go_btn, 84, 28);
    lv_obj_set_pos(go_btn, 210, 0);
    Theme::styleTouchButton(go_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(go_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self && self->_compose_ta) {
        strncpy(self->_store.uiPrefs().search_query, lv_textarea_get_text(self->_compose_ta),
                sizeof(self->_store.uiPrefs().search_query) - 1);
        self->_store.uiPrefs().search_active = (self->_store.uiPrefs().search_query[0] != '\0');
        self->_chats_subpage = false;
        self->renderBody();
      }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* gol = lv_label_create(go_btn);
    lv_obj_set_style_text_font(gol, &faro_font_12, 0);
    lv_label_set_text(gol, "Search");
    lv_obj_center(gol);

    lv_obj_t* imp_btn = lv_button_create(scard);
    lv_obj_set_size(imp_btn, 144, 38);
    lv_obj_set_pos(imp_btn, 0, 36);
    Theme::styleTouchButton(imp_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(imp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self->_store.uiPrefs().clipboard[0]) {
        self->_actions.importContactUri(self->_store.uiPrefs().clipboard);
      }
      self->_chats_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* iml = lv_label_create(imp_btn);
    lv_obj_set_style_text_font(iml, &faro_font_12, 0);
    lv_label_set_text(iml, "Import URI (+URI)");
    lv_obj_center(iml);

    lv_obj_t* dir_btn = lv_button_create(scard);
    lv_obj_set_size(dir_btn, 144, 38);
    lv_obj_set_pos(dir_btn, 150, 36);
    Theme::styleTouchButton(dir_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(dir_btn, [](lv_event_t* e) {
      static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Contacts);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* dl = lv_label_create(dir_btn);
    lv_obj_set_style_text_font(dl, &faro_font_12, 0);
    lv_label_set_text(dl, tr(StrId::BtnContacts, lang));
    lv_obj_center(dl);

    lv_obj_t* clr_btn = lv_button_create(scard);
    lv_obj_set_size(clr_btn, 294, 32);
    lv_obj_set_pos(clr_btn, 0, 80);
    Theme::styleTouchButton(clr_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().search_active = false;
      self->_store.uiPrefs().search_query[0] = '\0';
      self->_contact_filter = 0;
      self->_chats_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* clrl = lv_label_create(clr_btn);
    lv_obj_set_style_text_font(clrl, &faro_font_12, 0);
    lv_label_set_text(clrl, "Reset Search & Filters");
    lv_obj_center(clrl);
    return;
  }

  lv_obj_t* hdr = createHeaderRow(parent, tr(StrId::DirectMessages, lang), outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Home);
                                  }, this);

  // Single header button -> opens dedicated Chats Tools subpage
  lv_obj_t* tools_btn = lv_button_create(hdr);
  lv_obj_set_size(tools_btn, 62, 26);
  Theme::styleTouchButton(tools_btn, _store.uiPrefs().search_active ? p.warning : p.surface,
                          _store.uiPrefs().search_active ? lv_color_hex(0xFFFFFF) : p.accent);
  lv_obj_add_event_cb(tools_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_chats_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* tl_lbl = lv_label_create(tools_btn);
  lv_obj_set_style_text_font(tl_lbl, &faro_font_12, 0);
  lv_label_set_text(tl_lbl, "Tools");
  lv_obj_center(tl_lbl);

  // Role & Tag Filter Bar: 4 wide touch pills (74x26 px) [All] [Chat] [RPT/Room] [Fav*]
  lv_obj_t* fbar = lv_obj_create(parent);
  lv_obj_set_size(fbar, 308, 28);
  lv_obj_set_style_bg_opa(fbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(fbar, 0, 0);
  lv_obj_set_style_pad_all(fbar, 0, 0);
  lv_obj_set_flex_flow(fbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(fbar, LV_OBJ_FLAG_SCROLLABLE);

  static const char* kFilterNames[4] = {"All", "Chat", "RPT", "Fav*"};
  static const uint8_t kFilterMap[4] = {0, 1, 2, 5};
  struct FiltCtx { App* app; uint8_t f; };
  static FiltCtx s_fctx[4];
  for (uint8_t f = 0; f < 4; ++f) {
    lv_obj_t* fb = lv_button_create(fbar);
    lv_obj_set_size(fb, 74, 26);
    bool active = (_contact_filter == kFilterMap[f]);
    Theme::styleTouchButton(fb, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_fctx[f] = {this, kFilterMap[f]};
    lv_obj_add_event_cb(fb, [](lv_event_t* e) {
      auto* ctx = static_cast<FiltCtx*>(lv_event_get_user_data(e));
      ctx->app->_contact_filter = ctx->f;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_fctx[f]);
    lv_obj_t* fl = lv_label_create(fb);
    lv_obj_set_style_text_font(fl, &faro_font_12, 0);
    lv_label_set_text(fl, kFilterNames[f]);
    lv_obj_center(fl);
  }

    // Pinned Group Channel rows (Public, #ch, etc.) in Chats when filter == All (0)
    if (_contact_filter == 0) {
      struct ChatChanCtx { App* app; uint8_t idx; };
      static ChatChanCtx s_chat_ch_ctx[AppStore::kMaxChannels];
      for (size_t ci = 0; ci < AppStore::kMaxChannels; ++ci) {
        const auto& ch = _store.channels()[ci];
        if (!ch.valid) continue;
        if (_store.uiPrefs().search_active && _store.uiPrefs().search_query[0] != '\0') {
          if (strstr(ch.name, _store.uiPrefs().search_query) == nullptr) continue;
        }

        // Find latest message in this channel for preview
        const MessageRecord* last_msg = nullptr;
        for (size_t mi = _store.messageCount(); mi > 0; --mi) {
          const auto& m = _store.messageAt(mi - 1);
          if (m.valid && m.is_channel && m.channel_index == ch.index) {
            last_msg = &m;
            break;
          }
        }

        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, 308, 40);
        Theme::styleCard(row, outdoor);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_chat_ch_ctx[ci] = {this, ch.index};
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
          auto* ctx = static_cast<ChatChanCtx*>(lv_event_get_user_data(e));
          if (ctx && ctx->app) ctx->app->openChannelThread(ctx->idx);
        }, LV_EVENT_CLICKED, &s_chat_ch_ctx[ci]);

        createAvatarCircle(row, "#", uint8_t(0x20 + ch.index * 37));

        lv_obj_t* info = lv_label_create(row);
        lv_obj_set_width(info, 198);
        lv_obj_set_style_text_font(info, &faro_font_12, 0);
        lv_obj_set_style_text_color(info, p.text_primary, 0);
        if (last_msg) {
          lv_label_set_text_fmt(info, "[%u] %s | %s: %s",
                                unsigned(ch.index), ch.name, last_msg->sender_name, last_msg->text);
        } else {
          lv_label_set_text_fmt(info, "[%u] %s (%s) — Public/Group",
                                unsigned(ch.index), ch.name, ch.is_hashtag ? "#Tag" : "Chan");
        }
        lv_label_set_long_mode(info, LV_LABEL_LONG_CLIP);

        lv_obj_t* open_btn = lv_button_create(row);
        lv_obj_set_size(open_btn, 64, 28);
        Theme::styleTouchButton(open_btn, ch.unread_count > 0 ? p.success : p.accent, lv_color_hex(0xFFFFFF));
        lv_obj_add_event_cb(open_btn, [](lv_event_t* e) {
          auto* ctx = static_cast<ChatChanCtx*>(lv_event_get_user_data(e));
          if (ctx && ctx->app) ctx->app->openChannelThread(ctx->idx);
        }, LV_EVENT_CLICKED, &s_chat_ch_ctx[ci]);
        lv_obj_t* ol = lv_label_create(open_btn);
        lv_obj_set_style_text_font(ol, &faro_font_12, 0);
        lv_label_set_text_fmt(ol, ch.unread_count > 0 ? "Chat(%u)" : "Chat", unsigned(ch.unread_count));
        lv_obj_center(ol);
      }
    }

  struct PeerCtx { App* app; size_t idx; };
  static PeerCtx s_peer_ctx[AppStore::kMaxContacts];

  for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
    const auto& c = _store.contacts()[i];
    if (!c.valid) continue;
    if (_contact_filter == 1 && c.role != NodeRole::ChatNode) continue;
    if (_contact_filter == 2 && c.role != NodeRole::Repeater && c.role != NodeRole::RoomServer) continue;
    if (_contact_filter == 3 && c.role != NodeRole::RoomServer) continue;
    if (_contact_filter == 4 && c.role != NodeRole::Sensor) continue;
    if (_contact_filter == 5 && !c.favourite) continue;
    if (_contact_filter == 6 && c.tag_mask == 0) continue;
    if (_store.uiPrefs().search_active && _store.uiPrefs().search_query[0] != '\0') {
      if (strstr(c.name, _store.uiPrefs().search_query) == nullptr &&
          strstr(c.notes, _store.uiPrefs().search_query) == nullptr) {
        continue;
      }
    }

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 40);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_peer_ctx[i] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<PeerCtx*>(lv_event_get_user_data(e));
      if (!ctx || !ctx->app) return;
      ctx->app->openChatWithContact(ctx->idx);
    }, LV_EVENT_CLICKED, &s_peer_ctx[i]);

    createAvatarCircle(row, c.name, c.pub_key[0]);

    lv_obj_t* info = lv_label_create(row);
    lv_obj_set_width(info, 198);
    lv_obj_set_style_text_font(info, &faro_font_12, 0);
    lv_obj_set_style_text_color(info, p.text_primary, 0);
    int snr_db = int(c.last_snr_q4) / 4;
    bool dup_hash = _store.hasPathHashCollision(c.pub_key[0]);
    if (!c.hasPath()) {
      lv_label_set_text_fmt(info, "%s%s [%s] Fl | %+ddB%s",
                            c.favourite ? "*" : "", c.name, roleBadge(c.role), snr_db,
                            dup_hash ? " !DUP" : "");
    } else {
      lv_label_set_text_fmt(info, "%s%s [%s] %uh | %+ddB%s",
                            c.favourite ? "*" : "", c.name, roleBadge(c.role),
                            unsigned(c.pathHops()), snr_db, dup_hash ? " !DUP" : "");
    }
    lv_label_set_long_mode(info, LV_LABEL_LONG_CLIP);

    lv_obj_t* open_btn = lv_button_create(row);
    lv_obj_set_size(open_btn, 64, 28);
    Theme::styleTouchButton(open_btn, c.unread_count > 0 ? p.success : p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(open_btn, [](lv_event_t* e) {
      auto* ctx = static_cast<PeerCtx*>(lv_event_get_user_data(e));
      if (!ctx || !ctx->app) return;
      const auto& peer = ctx->app->_store.contacts()[ctx->idx];
      if (peer.role == NodeRole::Repeater || peer.role == NodeRole::Sensor) {
        ctx->app->_selected_contact_idx = ctx->idx;
        ctx->app->_contact_detail_tab = 3;  // Open Remote Admin / CLI directly
        ctx->app->showScreen(ScreenId::ContactDetail);
      } else {
        ctx->app->openChatWithContact(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_peer_ctx[i]);
    lv_obj_t* ol = lv_label_create(open_btn);
    lv_obj_set_style_text_font(ol, &faro_font_12, 0);
    if (c.role == NodeRole::Repeater || c.role == NodeRole::Sensor) {
      lv_label_set_text(ol, "Admin");
    } else {
      lv_label_set_text_fmt(ol, c.unread_count > 0 ? "Chat(%u)" : "Chat", unsigned(c.unread_count));
    }
    lv_obj_center(ol);
  }
}

void App::buildChatThreadView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  const ContactRecord* peer = &_store.contacts()[0];
  if (_selected_contact_idx < AppStore::kMaxContacts && _store.contacts()[_selected_contact_idx].valid) {
    peer = &_store.contacts()[_selected_contact_idx];
  }

  if (_chat_thread_subpage) {
    createHeaderRow(parent, "Chat Options", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_chat_thread_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* ocard = lv_obj_create(parent);
    lv_obj_set_size(ocard, 308, 166);
    Theme::styleCard(ocard, outdoor);
    lv_obj_set_style_pad_all(ocard, 6, 0);
    lv_obj_remove_flag(ocard, LV_OBJ_FLAG_SCROLLABLE);

    struct CTOptCtx { App* app; int idx; };
    static CTOptCtx s_ctctx[4];
    const char* kCTLabels[4] = {
      "Contact Details & QR",
      "Reset Path to Flood",
      _store.uiPrefs().show_timestamps ? "Timestamps: ON" : "Timestamps: OFF",
      "Remote Admin / CLI"
    };
    static const uint32_t kCTCols[4] = {0x1A73E8, 0xE37400, 0x1E8E3E, 0x9334E6};

    for (int i = 0; i < 4; ++i) {
      lv_obj_t* b = lv_button_create(ocard);
      lv_obj_set_size(b, 294, 34);
      lv_obj_set_pos(b, 0, i * 38);
      Theme::styleTouchButton(b, lv_color_hex(kCTCols[i]), lv_color_hex(0xFFFFFF));
      s_ctctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<CTOptCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        a->_chat_thread_subpage = false;
        if (ctx->idx == 0) {
          a->_contact_detail_tab = 0;
          a->openContactDetail(a->_selected_contact_idx);
        } else if (ctx->idx == 1) {
          const auto& c = a->_store.contacts()[a->_selected_contact_idx];
          a->_actions.resetContactPath(c.pub_key);
          a->renderBody();
        } else if (ctx->idx == 2) {
          a->_store.uiPrefs().show_timestamps = !a->_store.uiPrefs().show_timestamps;
          a->renderBody();
        } else if (ctx->idx == 3) {
          a->_contact_detail_tab = 3;
          a->openContactDetail(a->_selected_contact_idx);
        }
      }, LV_EVENT_CLICKED, &s_ctctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_14, 0);
      lv_label_set_text(l, kCTLabels[i]);
      lv_obj_center(l);
    }
    return;
  }

  char hdr_title[64] = {0};
  char batt[12] = {0};
  if (peer->remote_batt_mv > 0) snprintf(batt, sizeof(batt), " %.2fV", double(peer->remote_batt_mv) / 1000.0);
  if (!peer->hasPath()) {
    snprintf(hdr_title, sizeof(hdr_title), "%.14s [?]%s", peer->name[0] ? peer->name : "Peer", batt);
  } else {
    snprintf(hdr_title, sizeof(hdr_title), "%.14s [%u]%s", peer->name[0] ? peer->name : "Peer",
             unsigned(peer->pathHops()), batt);
  }

  lv_obj_t* hdr = createHeaderRow(parent, hdr_title, outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Chats);
                                  }, this);

  // Single header button -> opens dedicated Chat Options subpage
  lv_obj_t* opt_btn = lv_button_create(hdr);
  lv_obj_set_size(opt_btn, 64, 26);
  Theme::styleTouchButton(opt_btn, p.surface, p.accent);
  lv_obj_add_event_cb(opt_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_chat_thread_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* ol = lv_label_create(opt_btn);
  lv_obj_set_style_text_font(ol, &faro_font_12, 0);
  lv_label_set_text(ol, "Options");
  lv_obj_center(ol);

  // Optional Canned / Diacritics / Clipboard Drawer (when _show_compose_helper == true)
  if (_show_compose_helper) {
    lv_obj_t* hbox = lv_obj_create(parent);
    lv_obj_set_size(hbox, 308, 96);
    Theme::styleCard(hbox, outdoor);
    lv_obj_set_style_pad_all(hbox, 3, 0);
    lv_obj_set_flex_flow(hbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hbox, 3, 0);

    // Diacritics & Clipboard bar
    lv_obj_t* dbar = lv_obj_create(hbox);
    lv_obj_set_size(dbar, 294, 24);
    lv_obj_set_style_bg_opa(dbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dbar, 0, 0);
    lv_obj_set_style_pad_all(dbar, 0, 0);
    lv_obj_set_flex_flow(dbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(dbar, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kDiacritics[6] = {"ä", "ö", "ü", "è", "é", "Paste"};
    struct DiaCtx { App* app; const char* s; };
    static DiaCtx s_dctx[6];
    for (int d = 0; d < 6; ++d) {
      lv_obj_t* db = lv_button_create(dbar);
      lv_obj_set_size(db, d == 5 ? 54 : 42, 22);
      Theme::styleTouchButton(db, p.surface_alt, p.accent);
      s_dctx[d] = {this, kDiacritics[d]};
      lv_obj_add_event_cb(db, [](lv_event_t* e) {
        auto* ctx = static_cast<DiaCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        if (strcmp(ctx->s, "Paste") == 0) {
          if (a->_compose_ta && a->_store.uiPrefs().clipboard[0]) {
            lv_textarea_add_text(a->_compose_ta, a->_store.uiPrefs().clipboard);
          }
        } else if (a->_compose_ta) {
          lv_textarea_add_text(a->_compose_ta, ctx->s);
        }
        a->_show_compose_helper = false;
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_dctx[d]);
      lv_obj_t* dlbl = lv_label_create(db);
      lv_obj_set_style_text_font(dlbl, &faro_font_12, 0);
      lv_label_set_text(dlbl, kDiacritics[d]);
      lv_obj_center(dlbl);
    }

    // All Canned Messages list
    struct CanCtx { App* app; size_t idx; };
    static CanCtx s_can_ctx[AppStore::kMaxCanned];
    for (size_t ci = 0; ci < _store.cannedCount(); ++ci) {
      lv_obj_t* cb = lv_button_create(hbox);
      lv_obj_set_size(cb, 294, 22);
      Theme::styleTouchButton(cb, p.surface_alt, p.text_primary);
      s_can_ctx[ci] = {this, ci};
      lv_obj_add_event_cb(cb, [](lv_event_t* e) {
        auto* ctx = static_cast<CanCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        const auto& c = a->_store.contacts()[a->_selected_contact_idx];
        uint32_t crc = 0;
        DeliveryState st = DeliveryState::Queued;
        a->_actions.sendDirectMessage(c.pub_key, a->_store.cannedAt(ctx->idx), crc, st);
        a->_show_compose_helper = false;
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_can_ctx[ci]);
      lv_obj_t* clbl = lv_label_create(cb);
      lv_obj_set_style_text_font(clbl, &faro_font_12, 0);
      lv_label_set_text(clbl, _store.cannedAt(ci));
      lv_obj_center(clbl);
    }
  } else {
    // Message stream container (full height 124px since bottom nav is hidden in thread view)
    lv_obj_t* msg_box = lv_obj_create(parent);
    lv_obj_set_size(msg_box, 308, 124);
    Theme::styleCard(msg_box, outdoor);
    lv_obj_set_style_pad_all(msg_box, 4, 0);
    lv_obj_set_flex_flow(msg_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(msg_box, 3, 0);

    bool show_ts = _store.uiPrefs().show_timestamps;
    for (size_t i = 0; i < _store.messageCount(); ++i) {
      const auto& m = _store.messageAt(i);
      if (!m.valid || m.is_channel) continue;
      if (memcmp(m.peer_prefix, peer->pub_key, 6) != 0) continue;

      lv_obj_t* bubble = lv_label_create(msg_box);
      lv_obj_set_width(bubble, 290);
      lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(bubble, &faro_font_12, 0);
      lv_obj_set_style_text_color(bubble, m.outgoing ? p.accent : p.text_primary, 0);
      uint32_t hh = (m.timestamp / 3600U) % 24U;
      uint32_t mm = (m.timestamp / 60U) % 60U;
      if (m.outgoing) {
        unsigned heard_cnt = unsigned(m.repeats_heard + (m.delivery_state == DeliveryState::DeliveredAck ? 1 : 0));
        if (show_ts) {
          lv_label_set_text_fmt(bubble, "[%02u:%02u] Me [%s | Heard x%u]: %s",
                                unsigned(hh), unsigned(mm), deliveryBadge(m.delivery_state), heard_cnt, m.text);
        } else {
          lv_label_set_text_fmt(bubble, "Me [%s | Heard x%u]: %s", deliveryBadge(m.delivery_state), heard_cnt, m.text);
        }
      } else {
        if (show_ts) {
          lv_label_set_text_fmt(bubble, "[%02u:%02u] %s (%s%uh): %s",
                                unsigned(hh), unsigned(mm), m.sender_name,
                                m.hops == 0 ? "->" : "", unsigned(m.hops), m.text);
        } else {
          lv_label_set_text_fmt(bubble, "%s (%uh): %s", m.sender_name, unsigned(m.hops), m.text);
        }
      }
    }
    lv_obj_scroll_to_y(msg_box, LV_COORD_MAX, LV_ANIM_OFF);
  }

  // Bottom compose bar: Textarea (150 char limit) + [Canned/Ä] + [Send] (34px tall)
  lv_obj_t* comp = lv_obj_create(parent);
  lv_obj_set_size(comp, 308, 34);
  lv_obj_set_style_bg_opa(comp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(comp, 0, 0);
  lv_obj_set_style_pad_all(comp, 0, 0);
  lv_obj_set_flex_flow(comp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(comp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(comp, LV_OBJ_FLAG_SCROLLABLE);

  _compose_ta = lv_textarea_create(comp);
  lv_obj_set_size(_compose_ta, 166, 32);
  lv_textarea_set_one_line(_compose_ta, true);
  lv_textarea_set_max_length(_compose_ta, 150);
  lv_textarea_set_placeholder_text(_compose_ta, "Message (max 150)...");
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* can_btn = lv_button_create(comp);
  lv_obj_set_size(can_btn, 62, 32);
  Theme::styleTouchButton(can_btn, _show_compose_helper ? p.accent : p.surface_alt,
                          _show_compose_helper ? lv_color_hex(0xFFFFFF) : p.text_primary);
  lv_obj_add_event_cb(can_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_show_compose_helper = !self->_show_compose_helper;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* cl = lv_label_create(can_btn);
  lv_obj_set_style_text_font(cl, &faro_font_12, 0);
  lv_label_set_text(cl, "Can/ä");
  lv_obj_center(cl);

  lv_obj_t* send_btn = lv_button_create(comp);
  lv_obj_set_size(send_btn, 72, 32);
  Theme::styleTouchButton(send_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self || !self->_compose_ta) return;
    const char* cur = lv_textarea_get_text(self->_compose_ta);
    if (!cur || !cur[0]) return;  // empty box: nothing to send
    char txt[sizeof(self->_draft_text)] = {0};
    strncpy(txt, cur, sizeof(txt) - 1);
    lv_textarea_set_text(self->_compose_ta, "");
    const auto& c = self->_store.contacts()[self->_selected_contact_idx];
    uint32_t crc = 0;
    DeliveryState st = DeliveryState::Queued;
    self->_actions.sendDirectMessage(c.pub_key, txt, crc, st);
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* sl = lv_label_create(send_btn);
  lv_obj_set_style_text_font(sl, &faro_font_12, 0);
  lv_label_set_text(sl, tr(StrId::BtnSend, lang));
  lv_obj_center(sl);
}

void App::buildChannelsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  if (_channels_subpage) {
    createHeaderRow(parent, "Channel Setup / #ch", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_channels_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* ccard = lv_obj_create(parent);
    lv_obj_set_size(ccard, 308, 128);
    Theme::styleCard(ccard, outdoor);
    lv_obj_set_style_pad_all(ccard, 6, 0);
    lv_obj_remove_flag(ccard, LV_OBJ_FLAG_SCROLLABLE);

    _compose_ta = lv_textarea_create(ccard);
    lv_obj_set_size(_compose_ta, 206, 28);
    lv_obj_set_pos(_compose_ta, 0, 0);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_placeholder_text(_compose_ta, "#ch, #alps, #eu or name...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* htag_btn = lv_button_create(ccard);
    lv_obj_set_size(htag_btn, 84, 28);
    lv_obj_set_pos(htag_btn, 210, 0);
    Theme::styleTouchButton(htag_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(htag_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char nm[24] = {0};
      const char* txt = self->_compose_ta ? lv_textarea_get_text(self->_compose_ta) : nullptr;
      if (txt && txt[0]) {
        snprintf(nm, sizeof(nm), "%s%s", txt[0] == '#' ? "" : "#", txt);
      } else {
        strncpy(nm, "#ch", sizeof(nm) - 1);
      }
      self->_actions.addHashtagChannel(nm);
      self->_channels_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* htl = lv_label_create(htag_btn);
    lv_obj_set_style_text_font(htl, &faro_font_12, 0);
    lv_label_set_text(htl, "+ Join #Tag");
    lv_obj_center(htl);

    // Quick 1-tap regional hashtag presets: [#ch] [#eu] [#alps] [#test]
    static const char* kQuickTags[4] = {"#ch", "#eu", "#alps", "#test"};
    struct QTagCtx { App* app; const char* tag; };
    static QTagCtx s_qtag_ctx[4];
    for (int q = 0; q < 4; ++q) {
      lv_obj_t* qb = lv_button_create(ccard);
      lv_obj_set_size(qb, 70, 34);
      lv_obj_set_pos(qb, q * 74, 36);
      Theme::styleTouchButton(qb, q == 0 ? p.success : p.surface_alt,
                              q == 0 ? lv_color_hex(0xFFFFFF) : p.text_primary);
      s_qtag_ctx[q] = {this, kQuickTags[q]};
      lv_obj_add_event_cb(qb, [](lv_event_t* e) {
        auto* ctx = static_cast<QTagCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        ctx->app->_actions.addHashtagChannel(ctx->tag);
        ctx->app->_channels_subpage = false;
        ctx->app->renderBody();
      }, LV_EVENT_CLICKED, &s_qtag_ctx[q]);
      lv_obj_t* ql = lv_label_create(qb);
      lv_obj_set_style_text_font(ql, &faro_font_14, 0);
      lv_label_set_text(ql, kQuickTags[q]);
      lv_obj_center(ql);
    }

    lv_obj_t* aes_btn = lv_button_create(ccard);
    lv_obj_set_size(aes_btn, 294, 36);
    lv_obj_set_pos(aes_btn, 0, 78);
    Theme::styleTouchButton(aes_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(aes_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char nm[24] = {0};
      const char* txt = self->_compose_ta ? lv_textarea_get_text(self->_compose_ta) : nullptr;
      if (txt && txt[0]) {
        strncpy(nm, txt, sizeof(nm) - 1);
      } else {
        snprintf(nm, sizeof(nm), "Priv-%zu", self->_store.channelCount());
      }
      if (strcasecmp(nm, "Public") == 0 || nm[0] == '#') {
        // "Public" and #hashtag names have well-known keys; a private channel must not shadow them
        strncpy(self->_store.status().last_cli_reply, "Use another name: Public/#name keys are public.",
                sizeof(self->_store.status().last_cli_reply) - 1);
        self->renderBody();
        return;
      }
      uint8_t key[16];
      fillRandom(key, sizeof(key));
      self->_actions.addChannel(nm, key, 16);
      memset(key, 0, sizeof(key));
      self->_channels_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* aesl = lv_label_create(aes_btn);
    lv_obj_set_style_text_font(aesl, &faro_font_14, 0);
    lv_label_set_text(aesl, "+ New Private AES-128 Channel");
    lv_obj_center(aesl);
    return;
  }

  lv_obj_t* hdr = createHeaderRow(parent, tr(StrId::GroupChannels, lang), outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Home);
                                  }, this);

  // Single header button -> opens dedicated Channel Setup / #ch subpage
  lv_obj_t* new_btn = lv_button_create(hdr);
  lv_obj_set_size(new_btn, 76, 26);
  Theme::styleTouchButton(new_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(new_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_channels_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* nwl = lv_label_create(new_btn);
  lv_obj_set_style_text_font(nwl, &faro_font_12, 0);
  lv_label_set_text(nwl, "+ #ch / Add");
  lv_obj_center(nwl);

  struct ChanCtx { App* app; uint8_t idx; };
  static ChanCtx s_ch_ctx[AppStore::kMaxChannels];

  for (size_t i = 0; i < AppStore::kMaxChannels; ++i) {
    const auto& ch = _store.channels()[i];
    if (!ch.valid) continue;

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 42);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_ch_ctx[i] = {this, ch.index};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<ChanCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) ctx->app->openChannelThread(ctx->idx);
    }, LV_EVENT_CLICKED, &s_ch_ctx[i]);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_width(lbl, 224);
    lv_obj_set_style_text_font(lbl, &faro_font_14, 0);
    lv_obj_set_style_text_color(lbl, p.text_primary, 0);
    lv_label_set_text_fmt(lbl, "[%u] %s (%s)",
                          unsigned(ch.index), ch.name,
                          ch.is_hashtag ? "#Tag" : (ch.secret_len == 32 ? "AES256" : "AES128"));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);

    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, 64, 30);
    Theme::styleTouchButton(btn, ch.unread_count > 0 ? p.success : p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      auto* ctx = static_cast<ChanCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) ctx->app->openChannelThread(ctx->idx);
    }, LV_EVENT_CLICKED, &s_ch_ctx[i]);
    lv_obj_t* bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text_fmt(bl, ch.unread_count > 0 ? "Open(%u)" : "Open", unsigned(ch.unread_count));
    lv_obj_center(bl);
  }
}

void App::buildChannelThreadView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  const ChannelRecord* ch = _store.findChannel(_selected_channel_idx);
  const char* ch_name = ch ? ch->name : "Public";

  if (_channel_thread_subpage) {
    createHeaderRow(parent, "Channel Settings", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_channel_thread_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* ocard = lv_obj_create(parent);
    lv_obj_set_size(ocard, 308, 166);
    Theme::styleCard(ocard, outdoor);
    lv_obj_set_style_pad_all(ocard, 6, 0);
    lv_obj_remove_flag(ocard, LV_OBJ_FLAG_SCROLLABLE);

    _compose_ta = lv_textarea_create(ocard);
    lv_obj_set_size(_compose_ta, 200, 28);
    lv_obj_set_pos(_compose_ta, 0, 2);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, (ch && ch->name[0] == '#') ? ch->name : "#ch");
    lv_textarea_set_placeholder_text(_compose_ta, "#ch or #hashtag...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* set_tag_btn = lv_button_create(ocard);
    lv_obj_set_size(set_tag_btn, 90, 28);
    lv_obj_set_pos(set_tag_btn, 204, 2);
    Theme::styleTouchButton(set_tag_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(set_tag_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const char* txt = self->_compose_ta ? lv_textarea_get_text(self->_compose_ta) : "#ch";
      if (!txt || !txt[0]) txt = "#ch";
      self->_actions.addHashtagChannel(txt);
      self->_channel_thread_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* stl = lv_label_create(set_tag_btn);
    lv_obj_set_style_text_font(stl, &faro_font_12, 0);
    lv_label_set_text(stl, "Join #Tag");
    lv_obj_center(stl);

    lv_obj_t* mnt_btn = lv_button_create(ocard);
    lv_obj_set_size(mnt_btn, 144, 36);
    lv_obj_set_pos(mnt_btn, 0, 38);
    Theme::styleTouchButton(mnt_btn, _store.uiPrefs().mention_only_filter ? p.warning : p.surface_alt,
                            _store.uiPrefs().mention_only_filter ? lv_color_hex(0xFFFFFF) : p.text_primary);
    lv_obj_add_event_cb(mnt_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().mention_only_filter = !self->_store.uiPrefs().mention_only_filter;
      self->_channel_thread_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* mntl = lv_label_create(mnt_btn);
    lv_obj_set_style_text_font(mntl, &faro_font_12, 0);
    lv_label_set_text(mntl, _store.uiPrefs().mention_only_filter ? "Filter: @Ment" : "Filter: All");
    lv_obj_center(mntl);

    lv_obj_t* scp_btn = lv_button_create(ocard);
    lv_obj_set_size(scp_btn, 144, 36);
    lv_obj_set_pos(scp_btn, 150, 38);
    Theme::styleTouchButton(scp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(scp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      static const char* kScopes[4] = {"*", "#eu-mesh", "#ch-alps", "offgrid"};
      for (size_t i = 0; i < AppStore::kMaxChannels; ++i) {
        auto& c = self->_store.channelsMutable()[i];
        if (c.valid && c.index == self->_selected_channel_idx) {
          size_t next = 0;
          for (size_t s = 0; s < 4; ++s) {
            if (strcmp(c.scope_name, kScopes[s]) == 0) {
              next = (s + 1) % 4;
              break;
            }
          }
          strncpy(c.scope_name, kScopes[next], sizeof(c.scope_name) - 1);
          self->_actions.setDefaultScope(c.scope_name);
          break;
        }
      }
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* scpl = lv_label_create(scp_btn);
    lv_obj_set_style_text_font(scpl, &faro_font_12, 0);
    lv_label_set_text_fmt(scpl, "Scope: %s", ch ? ch->scope_name : "*");
    lv_obj_center(scpl);

    lv_obj_t* add_ch_btn = lv_button_create(ocard);
    lv_obj_set_size(add_ch_btn, _selected_channel_idx > 0 ? 144 : 294, 36);
    lv_obj_set_pos(add_ch_btn, 0, 82);
    Theme::styleTouchButton(add_ch_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(add_ch_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_channel_thread_subpage = false;
      self->_channels_subpage = true;
      self->showScreen(ScreenId::Channels);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* acl = lv_label_create(add_ch_btn);
    lv_obj_set_style_text_font(acl, &faro_font_12, 0);
    lv_label_set_text(acl, "+ All Channels / #ch Setup");
    lv_obj_center(acl);

    if (_selected_channel_idx > 0) {
      lv_obj_t* del_btn = lv_button_create(ocard);
      lv_obj_set_size(del_btn, 144, 36);
      lv_obj_set_pos(del_btn, 150, 82);
      Theme::styleTouchButton(del_btn, p.danger, lv_color_hex(0xFFFFFF));
      lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
        auto* self = static_cast<App*>(lv_event_get_user_data(e));
        self->_actions.removeChannel(self->_selected_channel_idx);
        self->showScreen(ScreenId::Channels);
      }, LV_EVENT_CLICKED, this);
      lv_obj_t* dell = lv_label_create(del_btn);
      lv_obj_set_style_text_font(dell, &faro_font_12, 0);
      lv_label_set_text(dell, "Delete Channel");
      lv_obj_center(dell);
    }

    char sec_hex[36] = {0};
    if (ch) {
      AppStore::bytesToHex(ch->secret, 8, sec_hex, sizeof(sec_hex));
    }
    lv_obj_t* key_info = lv_label_create(ocard);
    lv_obj_set_pos(key_info, 2, 126);
    lv_obj_set_style_text_font(key_info, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(key_info, p.text_secondary, 0);
    lv_label_set_text_fmt(key_info, "Slot #%u | %s | Key:%s..",
                          unsigned(_selected_channel_idx), ch_name, sec_hex[0] ? sec_hex : "default");
    return;
  }

  lv_obj_t* hdr = createHeaderRow(parent, ch_name, outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Channels);
                                  }, this);

  // Single header button -> opens dedicated Channel Options subpage
  lv_obj_t* opt_btn = lv_button_create(hdr);
  lv_obj_set_size(opt_btn, 64, 26);
  Theme::styleTouchButton(opt_btn, p.surface, p.accent);
  lv_obj_add_event_cb(opt_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_channel_thread_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* ol = lv_label_create(opt_btn);
  lv_obj_set_style_text_font(ol, &faro_font_12, 0);
  lv_label_set_text(ol, "Options");
  lv_obj_center(ol);

  lv_obj_t* msg_box = lv_obj_create(parent);
  lv_obj_set_size(msg_box, 308, 124);
  Theme::styleCard(msg_box, outdoor);
  lv_obj_set_style_pad_all(msg_box, 4, 0);
  lv_obj_set_flex_flow(msg_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(msg_box, 3, 0);

  const char* my_name = _store.status().node_name;
  struct ReplyCtx { App* app; const char* sender; };
  static ReplyCtx s_rep_ctx[32];
  size_t rep_slot = 0;
  size_t shown_msgs = 0;

  for (size_t i = 0; i < _store.messageCount(); ++i) {
    const auto& m = _store.messageAt(i);
    if (!m.valid || !m.is_channel || m.channel_index != _selected_channel_idx) continue;

    bool is_mention = (my_name[0] != '\0' && strstr(m.text, my_name) != nullptr) ||
                      (strstr(m.text, "@[") != nullptr);
    if (_store.uiPrefs().mention_only_filter && !is_mention) continue;

    ++shown_msgs;
    lv_obj_t* lbl = lv_label_create(msg_box);
    lv_obj_set_width(lbl, 290);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(lbl, is_mention ? p.warning : (m.outgoing ? p.accent : p.text_primary), 0);
    if (m.outgoing) {
      lv_label_set_text_fmt(lbl, "%s [Heard x%u]: %s", m.sender_name, unsigned(m.repeats_heard + 1), m.text);
    } else {
      lv_label_set_text_fmt(lbl, "%s%u> %s: %s", is_mention ? "@! " : "", unsigned(m.hops), m.sender_name, m.text);
      if (rep_slot < 32) {
        s_rep_ctx[rep_slot] = {this, m.sender_name};
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl, [](lv_event_t* e) {
          auto* ctx = static_cast<ReplyCtx*>(lv_event_get_user_data(e));
          if (ctx && ctx->app && ctx->app->_compose_ta && ctx->sender && ctx->sender[0]) {
            char mtag[44];
            snprintf(mtag, sizeof(mtag), "@[%s] ", ctx->sender);
            lv_textarea_add_text(ctx->app->_compose_ta, mtag);
          }
        }, LV_EVENT_CLICKED, &s_rep_ctx[rep_slot]);
        ++rep_slot;
      }
    }
  }
  if (shown_msgs == 0) {
    lv_obj_t* empty_lbl = lv_label_create(msg_box);
    lv_obj_set_width(empty_lbl, 290);
    lv_label_set_long_mode(empty_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(empty_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(empty_lbl, p.text_secondary, 0);
    lv_label_set_text_fmt(empty_lbl, "[%s] Active on LoRa (%.3f MHz). No messages received yet — type below to broadcast.",
                          ch_name, double(_store.radio().freq_mhz));
  }
  lv_obj_scroll_to_y(msg_box, LV_COORD_MAX, LV_ANIM_OFF);

  lv_obj_t* comp = lv_obj_create(parent);
  lv_obj_set_size(comp, 308, 34);
  lv_obj_set_style_bg_opa(comp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(comp, 0, 0);
  lv_obj_set_style_pad_all(comp, 0, 0);
  lv_obj_set_flex_flow(comp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(comp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(comp, LV_OBJ_FLAG_SCROLLABLE);

  _compose_ta = lv_textarea_create(comp);
  lv_obj_set_size(_compose_ta, 224, 32);
  lv_textarea_set_one_line(_compose_ta, true);
  lv_textarea_set_placeholder_text(_compose_ta, "Broadcast to channel...");
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* send_btn = lv_button_create(comp);
  lv_obj_set_size(send_btn, 76, 32);
  Theme::styleTouchButton(send_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self || !self->_compose_ta) return;
    const char* cur = lv_textarea_get_text(self->_compose_ta);
    if (!cur || !cur[0]) return;  // empty box: nothing to send
    char txt[sizeof(self->_draft_text)] = {0};
    strncpy(txt, cur, sizeof(txt) - 1);
    lv_textarea_set_text(self->_compose_ta, "");
    self->_actions.sendChannelMessage(self->_selected_channel_idx, txt);
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* sl = lv_label_create(send_btn);
  lv_obj_set_style_text_font(sl, &faro_font_12, 0);
  lv_label_set_text(sl, tr(StrId::BtnSend, lang));
  lv_obj_center(sl);
}

void App::buildDiscoverView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  if (_discover_subpage) {
    createHeaderRow(parent, "Advert & Scan", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_discover_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* dcard = lv_obj_create(parent);
    lv_obj_set_size(dcard, 308, 128);
    Theme::styleCard(dcard, outdoor);
    lv_obj_set_style_pad_all(dcard, 6, 0);
    lv_obj_remove_flag(dcard, LV_OBJ_FLAG_SCROLLABLE);

    struct DSubCtx { App* app; int idx; };
    static DSubCtx s_dsctx[4];
    const char* kDSubLabels[4] = {
      "Send 0-Hop Advert",
      "Send Flood Advert",
      "Scan Local Nodes",
      _discover_show_hash ? "Column: Path Hash" : "Column: SNR dB"
    };
    static const uint32_t kDSubCols[4] = {0x1A73E8, 0x1E8E3E, 0xE37400, 0x9334E6};

    for (int i = 0; i < 4; ++i) {
      lv_obj_t* b = lv_button_create(dcard);
      lv_obj_set_size(b, 144, 52);
      lv_obj_set_pos(b, (i % 2) * 150, (i / 2) * 58);
      Theme::styleTouchButton(b, lv_color_hex(kDSubCols[i]), lv_color_hex(0xFFFFFF));
      s_dsctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<DSubCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        if (ctx->idx == 0) {
          a->_actions.sendZeroHopAdvert();
          a->updateStatusBar();
          a->_discover_subpage = false;
        } else if (ctx->idx == 1) {
          a->_actions.sendFloodAdvert();
          a->updateStatusBar();
          a->_discover_subpage = false;
        } else if (ctx->idx == 2) {
          a->_actions.scanNearbyNodes();
          a->updateStatusBar();
          a->_discover_subpage = false;
        } else {
          a->_discover_show_hash = !a->_discover_show_hash;
          a->_discover_subpage = false;
        }
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_dsctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, kDSubLabels[i]);
      lv_obj_center(l);
    }
    return;
  }

  lv_obj_t* hdr = createHeaderRow(parent, tr(StrId::DiscoverMesh, lang), outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Home);
                                  }, this);

  // Single header button -> opens dedicated Advert & Scan subpage
  lv_obj_t* adv_btn = lv_button_create(hdr);
  lv_obj_set_size(adv_btn, 64, 26);
  Theme::styleTouchButton(adv_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(adv_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_discover_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* advl = lv_label_create(adv_btn);
  lv_obj_set_style_text_font(advl, &faro_font_12, 0);
  lv_label_set_text(advl, "Advert");
  lv_obj_center(advl);

  // Filter pills: 4 wide touch buttons (74x26 px) [All] [RPT] [Room] [Chat]
  lv_obj_t* fbar = lv_obj_create(parent);
  lv_obj_set_size(fbar, 308, 28);
  lv_obj_set_style_bg_opa(fbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(fbar, 0, 0);
  lv_obj_set_style_pad_all(fbar, 0, 0);
  lv_obj_set_flex_flow(fbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(fbar, LV_OBJ_FLAG_SCROLLABLE);

  static const char* kDFilters[4] = {"All", "RPT", "Room", "Chat"};
  struct DFiltCtx { App* app; uint8_t f; };
  static DFiltCtx s_dfctx[4];
  for (uint8_t f = 0; f < 4; ++f) {
    lv_obj_t* fb = lv_button_create(fbar);
    lv_obj_set_size(fb, 74, 26);
    bool active = (_discover_filter == f);
    Theme::styleTouchButton(fb, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_dfctx[f] = {this, f};
    lv_obj_add_event_cb(fb, [](lv_event_t* e) {
      auto* ctx = static_cast<DFiltCtx*>(lv_event_get_user_data(e));
      ctx->app->_discover_filter = ctx->f;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_dfctx[f]);
    lv_obj_t* fl = lv_label_create(fb);
    lv_obj_set_style_text_font(fl, &faro_font_12, 0);
    lv_label_set_text(fl, kDFilters[f]);
    lv_obj_center(fl);
  }

  // Answers to the last zero-hop scan (how each node heard us / how we heard it)
  for (size_t i = 0; i < AppStore::kMaxDiscovered; ++i) {
    const DiscoveredNode& d = _store.discovered()[i];
    if (!d.valid) continue;
    const ContactRecord* known = nullptr;
    for (size_t j = 0; j < AppStore::kMaxContacts && !known; ++j) {
      const auto& c = _store.contacts()[j];
      if (c.valid && memcmp(c.pub_key, d.prefix, 8) == 0) known = &c;
    }
    lv_obj_t* sl = lv_label_create(parent);
    lv_obj_set_width(sl, 300);
    lv_obj_set_style_text_font(sl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(sl, p.success, 0);
    char nm[16];
    if (known) snprintf(nm, sizeof(nm), "%.14s", known->name);
    else snprintf(nm, sizeof(nm), "%02X%02X%02X%02X", d.prefix[0], d.prefix[1], d.prefix[2], d.prefix[3]);
    lv_label_set_text_fmt(sl, "SCAN %-14.14s [%s] in %+ddB out %+ddB", nm, roleBadge(d.role),
                          int(d.snr_in_q4) / 4, int(d.snr_out_q4) / 4);
  }

  // Heard & Discovered Nodes List (whole row clickable + big Select button)
  struct DiscNodeCtx { App* app; size_t idx; };
  static DiscNodeCtx s_dn_ctx[AppStore::kMaxContacts];
  for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
    const auto& c = _store.contacts()[i];
    if (!c.valid) continue;
    if (_discover_filter == 1 && c.role != NodeRole::Repeater) continue;
    if (_discover_filter == 2 && c.role != NodeRole::RoomServer) continue;
    if (_discover_filter == 3 && c.role != NodeRole::ChatNode) continue;
    if (_discover_filter == 4 && c.role != NodeRole::Sensor) continue;

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 38);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_dn_ctx[i] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<DiscNodeCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_dn_ctx[i]);

    lv_obj_t* rl = lv_label_create(row);
    lv_obj_set_width(rl, 224);
    lv_obj_set_style_text_font(rl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(rl, c.role == NodeRole::Repeater ? p.success : p.text_primary, 0);

    uint8_t hops = c.pathHops();
    bool dup = _store.hasPathHashCollision(c.pub_key[0]);
    if (_discover_show_hash) {
      lv_label_set_text_fmt(rl, "%u> %-14.14s [%s] #%02X%s",
                            unsigned(hops), c.name, roleBadge(c.role),
                            unsigned(c.pub_key[0]), dup ? "!" : "");
    } else {
      lv_label_set_text_fmt(rl, "%u> %-14.14s [%s] %+ddB",
                            unsigned(hops), c.name, roleBadge(c.role),
                            int(c.last_snr_q4) / 4);
    }

    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, 62, 28);
    Theme::styleTouchButton(btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      auto* ctx = static_cast<DiscNodeCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_dn_ctx[i]);
    lv_obj_t* bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "Select");
    lv_obj_center(bl);
  }
}

void App::buildContactsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);
  const auto& g = _store.gps();

  if (_contacts_subpage) {
    createHeaderRow(parent, "Contacts Tools", outdoor,
                    [](lv_event_t* e) {
                      auto* self = static_cast<App*>(lv_event_get_user_data(e));
                      self->_contacts_subpage = false;
                      self->renderBody();
                    }, this);

    lv_obj_t* ccard = lv_obj_create(parent);
    lv_obj_set_size(ccard, 308, 128);
    Theme::styleCard(ccard, outdoor);
    lv_obj_set_style_pad_all(ccard, 6, 0);
    lv_obj_remove_flag(ccard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* js_btn = lv_button_create(ccard);
    lv_obj_set_size(js_btn, 294, 38);
    lv_obj_set_pos(js_btn, 0, 2);
    Theme::styleTouchButton(js_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(js_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char saved[96] = {0};
      self->_actions.exportContactsJson(saved, sizeof(saved));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* jsl = lv_label_create(js_btn);
    lv_obj_set_style_text_font(jsl, &faro_font_14, 0);
    lv_label_set_text(jsl, "Export Contacts to JSON");
    lv_obj_center(jsl);

    lv_obj_t* imp_btn = lv_button_create(ccard);
    lv_obj_set_size(imp_btn, 294, 38);
    lv_obj_set_pos(imp_btn, 0, 46);
    Theme::styleTouchButton(imp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(imp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto& st = self->_store.status();
      const char* clip = self->_store.uiPrefs().clipboard;
      if (!clip[0]) {
        snprintf(st.export_status, sizeof(st.export_status), "Clipboard empty: paste a meshcore:// URI first");
      } else if (self->_actions.importContactUri(clip)) {
        snprintf(st.export_status, sizeof(st.export_status), "Imported from clipboard");
      } else {
        snprintf(st.export_status, sizeof(st.export_status), "Not a contact / channel URI");
      }
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* iml = lv_label_create(imp_btn);
    lv_obj_set_style_text_font(iml, &faro_font_14, 0);
    lv_label_set_text(iml, "Import URI from clipboard");
    lv_obj_center(iml);

    lv_obj_t* stl = lv_label_create(ccard);
    lv_obj_set_pos(stl, 0, 90);
    lv_obj_set_width(stl, 294);
    lv_obj_set_style_text_font(stl, &faro_font_12, 0);
    lv_obj_set_style_text_color(stl, p.text_secondary, 0);
    lv_label_set_text(stl, _store.status().export_status[0] ? _store.status().export_status : "Exports go to the SD card (/faro/)");
    return;
  }

  lv_obj_t* hdr = createHeaderRow(parent, tr(StrId::ContactsDir, lang), outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Home);
                                  }, this);

  lv_obj_t* tools_btn = lv_button_create(hdr);
  lv_obj_set_size(tools_btn, 62, 26);
  Theme::styleTouchButton(tools_btn, _store.status().last_json_export[0] ? p.success : p.surface,
                          _store.status().last_json_export[0] ? lv_color_hex(0xFFFFFF) : p.accent);
  lv_obj_add_event_cb(tools_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_contacts_subpage = true;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* jsl = lv_label_create(tools_btn);
  lv_obj_set_style_text_font(jsl, &faro_font_12, 0);
  lv_label_set_text(jsl, "Tools");
  lv_obj_center(jsl);

  // Filter Bar: 4 wide touch pills (74x26 px) [All] [Chat] [RPT] [Fav*]
  lv_obj_t* fbar = lv_obj_create(parent);
  lv_obj_set_size(fbar, 308, 28);
  lv_obj_set_style_bg_opa(fbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(fbar, 0, 0);
  lv_obj_set_style_pad_all(fbar, 0, 0);
  lv_obj_set_flex_flow(fbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(fbar, LV_OBJ_FLAG_SCROLLABLE);

  static const char* kFilterNames[4] = {"All", "Chat", "RPT", "Fav*"};
  static const uint8_t kCFilterMap[4] = {0, 1, 2, 5};
  struct CFiltCtx { App* app; uint8_t f; };
  static CFiltCtx s_cfctx[4];
  for (uint8_t f = 0; f < 4; ++f) {
    lv_obj_t* fb = lv_button_create(fbar);
    lv_obj_set_size(fb, 74, 26);
    bool active = (_contact_filter == kCFilterMap[f]);
    Theme::styleTouchButton(fb, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_cfctx[f] = {this, kCFilterMap[f]};
    lv_obj_add_event_cb(fb, [](lv_event_t* e) {
      auto* ctx = static_cast<CFiltCtx*>(lv_event_get_user_data(e));
      ctx->app->_contact_filter = ctx->f;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_cfctx[f]);
    lv_obj_t* fl = lv_label_create(fb);
    lv_obj_set_style_text_font(fl, &faro_font_12, 0);
    lv_label_set_text(fl, kFilterNames[f]);
    lv_obj_center(fl);
  }

  struct ContactCtx { App* app; size_t idx; };
  static ContactCtx s_cctx[AppStore::kMaxContacts];
  static ContactCtx s_fav_ctx[AppStore::kMaxContacts];

  for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
    const auto& c = _store.contacts()[i];
    if (!c.valid) continue;
    if (_contact_filter == 1 && c.role != NodeRole::ChatNode) continue;
    if (_contact_filter == 2 && c.role != NodeRole::Repeater && c.role != NodeRole::RoomServer) continue;
    if (_contact_filter == 3 && c.role != NodeRole::RoomServer) continue;
    if (_contact_filter == 4 && c.role != NodeRole::Sensor) continue;
    if (_contact_filter == 5 && !c.favourite) continue;
    if (_contact_filter == 6 && c.tag_mask == 0) continue;
    if (_store.uiPrefs().search_active && _store.uiPrefs().search_query[0] != '\0') {
      if (strstr(c.name, _store.uiPrefs().search_query) == nullptr &&
          strstr(c.notes, _store.uiPrefs().search_query) == nullptr) {
        continue;
      }
    }

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 40);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_cctx[i] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<ContactCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_cctx[i]);

    double dist_km = 0.0;
    const char* card_dir = "--";
    if (g.valid && c.hasGps()) {
      dist_km = geomath::distanceMeters(g.lat_deg, g.lon_deg, c.latDeg(), c.lonDeg()) / 1000.0;
      double brng = geomath::bearingDegrees(g.lat_deg, g.lon_deg, c.latDeg(), c.lonDeg());
      card_dir = geomath::cardinalDirection(brng);
    }

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_width(lbl, 184);
    lv_obj_set_style_text_font(lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(lbl, p.text_primary, 0);
    lv_label_set_text_fmt(lbl, "%s [%s] %.1fkm %s", c.name, roleBadge(c.role), dist_km, card_dir);

    s_fav_ctx[i] = {this, i};
    lv_obj_t* fbtn = lv_button_create(row);
    lv_obj_set_size(fbtn, 38, 28);
    Theme::styleTouchButton(fbtn, c.favourite ? p.warning : p.surface_alt, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(fbtn, [](lv_event_t* e) {
      auto* ctx = static_cast<ContactCtx*>(lv_event_get_user_data(e));
      auto& peer = ctx->app->_store.contactsMutable()[ctx->idx];
      peer.favourite = !peer.favourite;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_fav_ctx[i]);
    lv_obj_t* flbl = lv_label_create(fbtn);
    lv_obj_set_style_text_font(flbl, &faro_font_12, 0);
    lv_label_set_text(flbl, c.favourite ? "*" : "+*");
    lv_obj_center(flbl);

    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, 60, 28);
    Theme::styleTouchButton(btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      auto* ctx = static_cast<ContactCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_cctx[i]);
    lv_obj_t* bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "Detail");
    lv_obj_center(bl);
  }
}

void App::buildContactDetailView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  ContactRecord* c = &_store.contactsMutable()[0];
  if (_selected_contact_idx < AppStore::kMaxContacts && _store.contacts()[_selected_contact_idx].valid) {
    c = &_store.contactsMutable()[_selected_contact_idx];
  }

  lv_obj_t* hdr = createHeaderRow(parent, c->name[0] ? c->name : tr(StrId::ContactDetails, lang), outdoor,
                                  [](lv_event_t* e) {
                                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Contacts);
                                  }, this);

  // 4 Sub-tabs inside Header / Sub-bar: [Info/QR] [Path] [Tags] [Admin/CLI]
  lv_obj_t* tbar = lv_obj_create(parent);
  lv_obj_set_size(tbar, 308, 24);
  lv_obj_set_style_bg_opa(tbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tbar, 0, 0);
  lv_obj_set_style_pad_all(tbar, 0, 0);
  lv_obj_set_flex_flow(tbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(tbar, LV_OBJ_FLAG_SCROLLABLE);

  static const char* kCDTabs[4] = {"Info/QR", "CustomPath", "Tags/Note", "Admin/CLI"};
  struct CDTabCtx { App* app; uint8_t t; };
  static CDTabCtx s_cdctx[4];
  for (uint8_t t = 0; t < 4; ++t) {
    lv_obj_t* tb = lv_button_create(tbar);
    lv_obj_set_size(tb, 74, 22);
    bool active = (_contact_detail_tab == t);
    Theme::styleTouchButton(tb, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_cdctx[t] = {this, t};
    lv_obj_add_event_cb(tb, [](lv_event_t* e) {
      auto* ctx = static_cast<CDTabCtx*>(lv_event_get_user_data(e));
      ctx->app->_contact_detail_tab = ctx->t;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_cdctx[t]);
    lv_obj_t* tl = lv_label_create(tb);
    lv_obj_set_style_text_font(tl, &faro_font_12, 0);
    lv_label_set_text(tl, kCDTabs[t]);
    lv_obj_center(tl);
  }
  (void)hdr;

  // TAB 0: Info, Share 0-Hop, Show on Map, Delete & QR Code
  if (_contact_detail_tab == 0) {
    lv_obj_t* split = lv_obj_create(parent);
    lv_obj_set_size(split, 308, 110);
    Theme::styleCard(split, outdoor);
    lv_obj_remove_flag(split, LV_OBJ_FLAG_SCROLLABLE);

    char hex_pre[16] = {0};
    AppStore::bytesToHex(c->pub_key, 6, hex_pre, sizeof(hex_pre));

    lv_obj_t* info = lv_label_create(split);
    lv_obj_set_pos(info, 0, 0);
    lv_obj_set_width(info, 200);
    lv_obj_set_style_text_font(info, &faro_font_12, 0);
    lv_obj_set_style_text_color(info, p.text_primary, 0);
    char extra[40] = {0};
    if (c->remote_batt_mv > 0) {
      snprintf(extra, sizeof(extra), "\nBatt %.2fV", double(c->remote_batt_mv) / 1000.0);
    }
    lv_label_set_text_fmt(info,
                          "%s [%s] #%02X\nKey: %s...\nPath: %s (%u hops)\nGPS: %.4f, %.4f%s",
                          c->name, roleBadge(c->role), unsigned(c->pub_key[0]), hex_pre,
                          c->hasPath() ? "Direct" : "Flood", unsigned(c->pathHops()),
                          c->latDeg(), c->lonDeg(), extra);

    // Action row: [Chat] [Share] [URI] [Map] [Del]
    lv_obj_t* chat_btn = lv_button_create(split);
    lv_obj_set_size(chat_btn, 38, 24);
    lv_obj_set_pos(chat_btn, 0, 72);
    Theme::styleTouchButton(chat_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(chat_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->openChatWithContact(self->_selected_contact_idx);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* chl = lv_label_create(chat_btn);
    lv_obj_set_style_text_font(chl, &faro_font_12, 0);
    lv_label_set_text(chl, "Chat");
    lv_obj_center(chl);

    lv_obj_t* shr_btn = lv_button_create(split);
    lv_obj_set_size(shr_btn, 44, 24);
    lv_obj_set_pos(shr_btn, 40, 72);
    Theme::styleTouchButton(shr_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(shr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      self->_actions.shareContactZeroHop(peer.pub_key);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* shl = lv_label_create(shr_btn);
    lv_obj_set_style_text_font(shl, &faro_font_12, 0);
    lv_label_set_text(shl, "Share");
    lv_obj_center(shl);

    lv_obj_t* uri_btn = lv_button_create(split);
    lv_obj_set_size(uri_btn, 36, 24);
    lv_obj_set_pos(uri_btn, 86, 72);
    Theme::styleTouchButton(uri_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(uri_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      char out_u[220] = {0};
      self->_actions.exportContactUri(peer.pub_key, out_u, sizeof(out_u));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* url = lv_label_create(uri_btn);
    lv_obj_set_style_text_font(url, &faro_font_12, 0);
    char my_uri[220] = {0};
    const bool in_clip = AppStore::formatContactUri(*c, my_uri, sizeof(my_uri)) &&
                         strncmp(_store.uiPrefs().clipboard, my_uri, sizeof(_store.uiPrefs().clipboard) - 1) == 0;
    lv_label_set_text(url, in_clip ? "OK" : "URI");
    lv_obj_center(url);

    lv_obj_t* map_btn = lv_button_create(split);
    lv_obj_set_size(map_btn, 36, 24);
    lv_obj_set_pos(map_btn, 124, 72);
    Theme::styleTouchButton(map_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(map_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->showScreen(ScreenId::Map);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* mpl = lv_label_create(map_btn);
    lv_obj_set_style_text_font(mpl, &faro_font_12, 0);
    lv_label_set_text(mpl, "Map");
    lv_obj_center(mpl);

    lv_obj_t* del_btn = lv_button_create(split);
    lv_obj_set_size(del_btn, 36, 24);
    lv_obj_set_pos(del_btn, 162, 72);
    Theme::styleTouchButton(del_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      self->_actions.removeContact(peer.pub_key);
      self->showScreen(ScreenId::Contacts);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* dll = lv_label_create(del_btn);
    lv_obj_set_style_text_font(dll, &faro_font_12, 0);
    lv_label_set_text(dll, "Del");
    lv_obj_center(dll);

    char uri[220] = {0};
    if (AppStore::formatContactUri(*c, uri, sizeof(uri))) {
      lv_obj_t* qr = lv_qrcode_create(split);
      lv_qrcode_set_size(qr, 84);
      lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
      lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
      lv_qrcode_update(qr, uri, uint32_t(strlen(uri)));
      lv_obj_set_pos(qr, 206, 4);
    }
    return;
  }

  // TAB 1: Custom Paths & Path Discovery / Trace (Ripple UI p.29-30)
  if (_contact_detail_tab == 1) {
    lv_obj_t* pcard = lv_obj_create(parent);
    lv_obj_set_size(pcard, 308, 110);
    Theme::styleCard(pcard, outdoor);
    lv_obj_remove_flag(pcard, LV_OBJ_FLAG_SCROLLABLE);

    char path_str[64] = {0};
    if (!c->hasPath()) {
      snprintf(path_str, sizeof(path_str), "Flood (auto-discover)");
    } else if (c->pathHops() == 0) {
      snprintf(path_str, sizeof(path_str), "Direct (0-hop)");
    } else {
      const uint8_t hs = c->pathHashSize();
      size_t pos = 0;
      for (uint8_t h = 0; h < c->pathHops() && h < 6 && pos + 2 * hs + 2 < sizeof(path_str); ++h) {
        if (h > 0) path_str[pos++] = ',';
        for (uint8_t b = 0; b < hs; ++b) {
          pos += size_t(snprintf(path_str + pos, sizeof(path_str) - pos, "%02X", unsigned(c->out_path[h * hs + b])));
        }
      }
    }

    lv_obj_t* plbl = lv_label_create(pcard);
    lv_obj_set_pos(plbl, 0, 0);
    lv_obj_set_style_text_font(plbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(plbl, p.text_primary, 0);
    lv_label_set_text_fmt(plbl, "Out-Path: %s", path_str);

    _compose_ta = lv_textarea_create(pcard);
    lv_obj_set_size(_compose_ta, 196, 26);
    lv_obj_set_pos(_compose_ta, 0, 20);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_placeholder_text(_compose_ta, "Hex hops e.g. C3,D4");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sp_btn = lv_button_create(pcard);
    lv_obj_set_size(sp_btn, 92, 26);
    lv_obj_set_pos(sp_btn, 202, 20);
    Theme::styleTouchButton(sp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self || !self->_compose_ta) return;
      const char* txt = lv_textarea_get_text(self->_compose_ta);
      ContactRecord peer = self->_store.contacts()[self->_selected_contact_idx];
      // One hop per comma-separated hex group; its length sets the path hash size (1-3 bytes)
      uint8_t buf[64] = {0};
      uint8_t hops = 0, hash_size = 0;
      const char* q = txt;
      bool ok = q != nullptr;
      while (ok && *q && hops < 16) {
        while (*q == ',' || *q == ' ') ++q;
        if (!*q) break;
        const char* start = q;
        while (*q && *q != ',' && *q != ' ') ++q;
        const size_t digits = size_t(q - start);
        const uint8_t hs = uint8_t(digits / 2);
        if (digits % 2 != 0 || hs < 1 || hs > 3 || (hash_size && hs != hash_size) ||
            size_t(hops + 1) * hs > sizeof(buf)) {
          ok = false;
          break;
        }
        hash_size = hs;
        char tmp[8] = {0};
        memcpy(tmp, start, digits);
        ok = AppStore::hexToBytes(tmp, &buf[hops * hs], hs);
        if (ok) ++hops;
      }
      if (!ok || hops == 0) {
        snprintf(self->_store.status().trace_status, sizeof(self->_store.status().trace_status),
                 "Path: hex hops like C3,D4 (or C3A1,D4B2)");
        self->renderBody();
        return;
      }
      peer.out_path_len = ContactRecord::encodePathLen(hash_size, hops);
      memset(peer.out_path, 0, sizeof(peer.out_path));
      memcpy(peer.out_path, buf, size_t(hops) * hash_size);
      self->_actions.addOrUpdateContact(peer);
      snprintf(self->_store.status().trace_status, sizeof(self->_store.status().trace_status),
               "Custom path set: %u hops", unsigned(hops));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* spl = lv_label_create(sp_btn);
    lv_obj_set_style_text_font(spl, &faro_font_12, 0);
    lv_label_set_text(spl, "Set Custom");
    lv_obj_center(spl);

    // Quick path buttons: [Flood] [0-Hop] [AdvPath] [Find] [Trace]
    lv_obj_t* b_fld = lv_button_create(pcard);
    lv_obj_set_size(b_fld, 54, 26);
    lv_obj_set_pos(b_fld, 0, 56);
    Theme::styleTouchButton(b_fld, p.warning, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(b_fld, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      self->_actions.resetContactPath(peer.pub_key);
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* l_fld = lv_label_create(b_fld);
    lv_obj_set_style_text_font(l_fld, &faro_font_12, 0);
    lv_label_set_text(l_fld, "Flood");
    lv_obj_center(l_fld);

    lv_obj_t* b_dir = lv_button_create(pcard);
    lv_obj_set_size(b_dir, 54, 26);
    lv_obj_set_pos(b_dir, 58, 56);
    Theme::styleTouchButton(b_dir, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(b_dir, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      ContactRecord peer = self->_store.contacts()[self->_selected_contact_idx];
      peer.out_path_len = 0;  // direct neighbour, no repeater in between
      self->_actions.addOrUpdateContact(peer);
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* l_dir = lv_label_create(b_dir);
    lv_obj_set_style_text_font(l_dir, &faro_font_12, 0);
    lv_label_set_text(l_dir, "0-Hop");
    lv_obj_center(l_dir);

    lv_obj_t* b_adv = lv_button_create(pcard);
    lv_obj_set_size(b_adv, 62, 26);
    lv_obj_set_pos(b_adv, 116, 56);
    Theme::styleTouchButton(b_adv, p.surface_alt, p.accent);
    lv_obj_add_event_cb(b_adv, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      self->_actions.useLastAdvertPath(peer.pub_key);
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* l_adv = lv_label_create(b_adv);
    lv_obj_set_style_text_font(l_adv, &faro_font_12, 0);
    lv_label_set_text(l_adv, "AdvPath");
    lv_obj_center(l_adv);

    lv_obj_t* b_fnd = lv_button_create(pcard);
    lv_obj_set_size(b_fnd, 56, 26);
    lv_obj_set_pos(b_fnd, 182, 56);
    Theme::styleTouchButton(b_fnd, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(b_fnd, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      self->_actions.findPath(peer.pub_key);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* l_fnd = lv_label_create(b_fnd);
    lv_obj_set_style_text_font(l_fnd, &faro_font_12, 0);
    lv_label_set_text(l_fnd, "Find");
    lv_obj_center(l_fnd);

    lv_obj_t* b_trc = lv_button_create(pcard);
    lv_obj_set_size(b_trc, 54, 26);
    lv_obj_set_pos(b_trc, 242, 56);
    Theme::styleTouchButton(b_trc, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(b_trc, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
      uint32_t tag = 0;
      if (self->_actions.sendTracePath(peer.pub_key, tag)) {
        self->showScreen(ScreenId::TracePath);
      } else {
        self->renderBody();  // trace_status says why
      }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* l_trc = lv_label_create(b_trc);
    lv_obj_set_style_text_font(l_trc, &faro_font_12, 0);
    lv_label_set_text(l_trc, "Trace");
    lv_obj_center(l_trc);

    lv_obj_t* tst = lv_label_create(pcard);
    lv_obj_set_pos(tst, 0, 84);
    lv_obj_set_width(tst, 294);
    lv_label_set_long_mode(tst, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(tst, &faro_font_12, 0);
    lv_obj_set_style_text_color(tst, p.text_secondary, 0);
    lv_label_set_text(tst, _store.status().trace_status[0] ? _store.status().trace_status
                                                           : "Find = path discovery, Trace = SNR per hop");
    return;
  }

  // TAB 2: Contact Tags & Free-Form Notes (Ripple UI v7.14+ p.12, 15)
  if (_contact_detail_tab == 2) {
    lv_obj_t* tcard = lv_obj_create(parent);
    lv_obj_set_size(tcard, 308, 110);
    Theme::styleCard(tcard, outdoor);
    lv_obj_remove_flag(tcard, LV_OBJ_FLAG_SCROLLABLE);

    struct TagToggleCtx { App* app; uint8_t bit; };
    static TagToggleCtx s_tgctx[3];
    for (uint8_t b = 0; b < 3; ++b) {
      lv_obj_t* tb = lv_button_create(tcard);
      lv_obj_set_size(tb, 94, 26);
      lv_obj_set_pos(tb, b * 100, 0);
      bool on = (c->tag_mask & (1U << b)) != 0;
      Theme::styleTouchButton(tb, on ? p.success : p.surface_alt,
                              on ? lv_color_hex(0xFFFFFF) : p.text_primary);
      s_tgctx[b] = {this, b};
      lv_obj_add_event_cb(tb, [](lv_event_t* e) {
        auto* ctx = static_cast<TagToggleCtx*>(lv_event_get_user_data(e));
        auto& peer = ctx->app->_store.contactsMutable()[ctx->app->_selected_contact_idx];
        peer.tag_mask ^= uint8_t(1U << ctx->bit);
        ctx->app->renderBody();
      }, LV_EVENT_CLICKED, &s_tgctx[b]);
      lv_obj_t* tl = lv_label_create(tb);
      lv_obj_set_style_text_font(tl, &faro_font_12, 0);
      lv_label_set_text_fmt(tl, "%s:%s", _store.uiPrefs().custom_tags[b], on ? "ON" : "OFF");
      lv_obj_center(tl);
    }

    _compose_ta = lv_textarea_create(tcard);
    lv_obj_set_size(_compose_ta, 214, 28);
    lv_obj_set_pos(_compose_ta, 0, 34);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, c->notes);
    lv_textarea_set_placeholder_text(_compose_ta, "Local contact note...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sn_btn = lv_button_create(tcard);
    lv_obj_set_size(sn_btn, 76, 28);
    lv_obj_set_pos(sn_btn, 220, 34);
    Theme::styleTouchButton(sn_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sn_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self || !self->_compose_ta) return;
      auto& peer = self->_store.contactsMutable()[self->_selected_contact_idx];
      strncpy(peer.notes, lv_textarea_get_text(self->_compose_ta), sizeof(peer.notes) - 1);
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* snl = lv_label_create(sn_btn);
    lv_obj_set_style_text_font(snl, &faro_font_12, 0);
    lv_label_set_text(snl, "Save Note");
    lv_obj_center(snl);

    lv_obj_t* n_lbl = lv_label_create(tcard);
    lv_obj_set_pos(n_lbl, 0, 68);
    lv_obj_set_width(n_lbl, 294);
    lv_obj_set_style_text_font(n_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(n_lbl, p.text_secondary, 0);
    lv_label_set_text_fmt(n_lbl, "Saved Note: %s", c->notes[0] ? c->notes : "(none)");
    return;
  }

  // TAB 3: Remote Admin & CLI (Room Server / Repeater / Sensor Admin — FREE in Faro! Ripple p.26-29)
  lv_obj_t* acard = lv_obj_create(parent);
  lv_obj_set_size(acard, 308, 110);
  Theme::styleCard(acard, outdoor);
  lv_obj_remove_flag(acard, LV_OBJ_FLAG_SCROLLABLE);

  _compose_ta = lv_textarea_create(acard);
  lv_obj_set_size(_compose_ta, 148, 24);
  lv_obj_set_pos(_compose_ta, 0, 0);
  lv_textarea_set_one_line(_compose_ta, true);
  lv_textarea_set_placeholder_text(_compose_ta, "Pwd or CLI cmd...");
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* lg_btn = lv_button_create(acard);
  lv_obj_set_size(lg_btn, 68, 24);
  lv_obj_set_pos(lg_btn, 152, 0);
  Theme::styleTouchButton(lg_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(lg_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
    const char* pwd = (self->_compose_ta && lv_textarea_get_text(self->_compose_ta)[0])
                          ? lv_textarea_get_text(self->_compose_ta)
                          : "";  // empty = guest login; never guess a default password
    self->_actions.sendRemoteLogin(peer.pub_key, pwd);
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* lgl = lv_label_create(lg_btn);
  lv_obj_set_style_text_font(lgl, &faro_font_12, 0);
  lv_label_set_text(lgl, "Login");
  lv_obj_center(lgl);

  lv_obj_t* cli_btn = lv_button_create(acard);
  lv_obj_set_size(cli_btn, 72, 24);
  lv_obj_set_pos(cli_btn, 224, 0);
  Theme::styleTouchButton(cli_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(cli_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    const auto& peer = self->_store.contacts()[self->_selected_contact_idx];
    const char* cmd = (self->_compose_ta && lv_textarea_get_text(self->_compose_ta)[0])
                          ? lv_textarea_get_text(self->_compose_ta)
                          : "ver";
    self->_actions.sendRemoteCli(peer.pub_key, cmd);
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* clil = lv_label_create(cli_btn);
  lv_obj_set_style_text_font(clil, &faro_font_12, 0);
  lv_label_set_text(clil, "Send CLI");
  lv_obj_center(clil);

  // Quick binary & remote queries: [Stats] [Tel/get] [Neighbors] [ACL] [MMA]
  static const char* kAdmBtns[5] = {"Stats", "Tel/get", "Neighb", "get acl", "get mma"};
  struct AdmBtnCtx { App* app; int idx; };
  static AdmBtnCtx s_abctx[5];
  for (int b = 0; b < 5; ++b) {
    lv_obj_t* ab = lv_button_create(acard);
    lv_obj_set_size(ab, 56, 22);
    lv_obj_set_pos(ab, b * 59, 28);
    Theme::styleTouchButton(ab, p.surface_alt, p.text_primary);
    s_abctx[b] = {this, b};
    lv_obj_add_event_cb(ab, [](lv_event_t* e) {
      auto* ctx = static_cast<AdmBtnCtx*>(lv_event_get_user_data(e));
      App* a = ctx->app;
      const auto& peer = a->_store.contacts()[a->_selected_contact_idx];
      if (ctx->idx == 0) a->_actions.requestRemoteStatus(peer.pub_key);
      else if (ctx->idx == 1) a->_actions.requestTelemetry(peer.pub_key);
      else if (ctx->idx == 2) a->_actions.requestRemoteBinary(peer.pub_key, "neighbors");
      else if (ctx->idx == 3) a->_actions.requestRemoteBinary(peer.pub_key, "acl");
      else if (ctx->idx == 4) a->_actions.requestRemoteBinary(peer.pub_key, "mma");
      a->renderBody();
    }, LV_EVENT_CLICKED, &s_abctx[b]);
    lv_obj_t* abl = lv_label_create(ab);
    lv_obj_set_style_text_font(abl, &faro_font_12, 0);
    lv_label_set_text(abl, kAdmBtns[b]);
    lv_obj_center(abl);
  }

  lv_obj_t* rlog = lv_label_create(acard);
  lv_obj_set_pos(rlog, 0, 54);
  lv_obj_set_width(rlog, 294);
  lv_label_set_long_mode(rlog, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(rlog, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(rlog, p.accent, 0);
  lv_label_set_text(rlog, _store.status().remote_admin_reply);
}

void App::buildTracePathView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);
  const auto& tr_res = _store.lastTrace();

  createHeaderRow(parent, tr(StrId::TracePathTitle, lang), outdoor,
                  [](lv_event_t* e) {
                    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::ContactDetail);
                  }, this);

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, 308, 134);
  Theme::styleCard(card, outdoor);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_obj_t* sum = lv_label_create(card);
  lv_obj_set_style_text_font(sum, &faro_font_14, 0);
  lv_obj_set_style_text_color(sum, p.accent, 0);
  if (tr_res.valid) {
    lv_label_set_text_fmt(sum, "Trace #%u: back over %u hops", unsigned(tr_res.tag), unsigned(tr_res.hop_count));
  } else {
    lv_label_set_text(sum, _store.status().trace_status[0] ? _store.status().trace_status : "No trace yet");
  }

  // Each hop: the repeater (first key byte) and the SNR it heard the previous sender with
  for (uint8_t i = 0; tr_res.valid && i < tr_res.hop_count && i < 7; ++i) {
    lv_obj_t* hl = lv_label_create(card);
    lv_obj_set_style_text_font(hl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(hl, p.text_primary, 0);
    const ContactRecord* rep = nullptr;
    for (size_t k = 0; k < AppStore::kMaxContacts && !rep; ++k) {
      const auto& c = _store.contacts()[k];
      if (c.valid && c.pub_key[0] == tr_res.hops[i].hash &&
          (c.role == NodeRole::Repeater || c.role == NodeRole::RoomServer)) {
        rep = &c;
      }
    }
    lv_label_set_text_fmt(hl, "%u: [%02X] %-12.12s SNR %+d dB", unsigned(i + 1), unsigned(tr_res.hops[i].hash),
                          rep ? rep->name : "", int(tr_res.hops[i].snr_q4) / 4);
  }
  if (tr_res.valid) {
    lv_obj_t* fl = lv_label_create(card);
    lv_obj_set_style_text_font(fl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(fl, p.success, 0);
    lv_label_set_text_fmt(fl, "Back to me: SNR %+d dB", int(tr_res.final_snr_q4) / 4);
  }
}

void App::buildMapView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  const auto& g = _store.gps();
  const auto& trk = _store.gpsTrack();
  const auto& st = _store.status();

  // Default to Switzerland (46.8182° N, 8.2275° E) when GPS has no fix.
  // If SD card tiles were discovered, auto-sync initial zoom and center on the SD card's
  // Switzerland tile area if (46.8182, 8.2275) is not covered at that zoom.
  double base_lat = 46.8182;
  double base_lon = 8.2275;
  double sd_lat = 0.0, sd_lon = 0.0;
  int sd_zoom = -1;
  bool has_sd_default = _tiles.getDefaultTileCenter(sd_lat, sd_lon, sd_zoom);
  if (!_map_sd_synced && has_sd_default && sd_zoom >= 1 && sd_zoom <= 18) {
    _map_zoom = sd_zoom;
    _map_sd_synced = true;
  }

  if (g.valid) {
    base_lat = g.lat_deg;
    base_lon = g.lon_deg;
  } else if (st.manual_lat_deg != 0.0 || st.manual_lon_deg != 0.0) {
    base_lat = st.manual_lat_deg;
    base_lon = st.manual_lon_deg;
  } else if (has_sd_default) {
    int ch_tx = 0, ch_ty = 0, ch_px = 0, ch_py = 0;
    geomath::latLonToTilePixel(base_lat, base_lon, _map_zoom, ch_tx, ch_ty, ch_px, ch_py);
    if (!_tiles.hasTile(_map_zoom, ch_tx, ch_ty)) {
      base_lat = sd_lat;
      base_lon = sd_lon;
    }
  }

  double clat = base_lat + _map_pan_lat_offset;
  double clon = base_lon + _map_pan_lon_offset;

  // Render slippy-tile / procedural viewport into _map_canvas_buf (308x138)
  _tiles.renderViewportRgb565(clat, clon, _map_zoom, _map_canvas_buf, 308, 138, outdoor);

  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, _map_canvas_buf, 308, 138, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_size(canvas, 308, 138);

  // Overlay GPS Track info & elevation badge + [GPX] export if enabled (Ripple UI p.31-33)
  if (_show_tracks_overlay && trk.point_count > 0) {
    lv_obj_t* trk_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(trk_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(trk_lbl, p.success, 0);
    lv_label_set_text_fmt(trk_lbl, "TRK%s: %.2fkm (%zu pts, %.0fm)%s",
                          trk.recording ? "[REC]" : "",
                          trk.distance_m / 1000.0, trk.point_count,
                          double(trk.points[trk.point_count - 1].alt_m),
                          _store.status().last_gpx_path[0] ? " [GPX OK]" : "");
    lv_obj_set_pos(trk_lbl, 4, 2);

    lv_obj_t* gpx_btn = lv_button_create(canvas);
    lv_obj_set_size(gpx_btn, 36, 18);
    lv_obj_set_pos(gpx_btn, 266, 2);
    Theme::styleTouchButton(gpx_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(gpx_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char saved[96] = {0};
      self->_actions.exportGpsTrackGpx(saved, sizeof(saved));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* gpxl = lv_label_create(gpx_btn);
    lv_obj_set_style_text_font(gpxl, &faro_font_12, 0);
    lv_label_set_text(gpxl, "GPX");
    lv_obj_center(gpxl);
  } else {
    lv_obj_t* src_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(src_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(src_lbl, _tiles.lastRenderUsedSdTiles() ? p.success : p.text_secondary, 0);
    lv_label_set_text_fmt(src_lbl, "%s | %s",
                          g.valid ? "GPS LOCK" : "CH Default",
                          _tiles.lastRenderUsedSdTiles() ? "SD Tiles OK" : "Grid Map");
    lv_obj_set_pos(src_lbl, 4, 2);
  }

  // Overlay self crosshair & contact pins on canvas
  lv_obj_t* self_pin = lv_label_create(canvas);
  lv_obj_set_style_text_font(self_pin, &faro_font_12, 0);
  lv_obj_set_style_text_color(self_pin, p.accent, 0);
  lv_label_set_text(self_pin, g.valid ? "[+] Me" : "[+] CH");
  lv_obj_set_pos(self_pin, 134, 64);

  int cx_t = 0, cy_t = 0, cx_px = 0, cy_px = 0;
  geomath::latLonToTilePixel(clat, clon, _map_zoom, cx_t, cy_t, cx_px, cy_px);
  int32_t world_cx = cx_t * 256 + cx_px;
  int32_t world_cy = cy_t * 256 + cy_px;

  int32_t used_sy[16] = {0};
  size_t used_count = 0;
  for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
    const auto& c = _store.contacts()[i];
    if (!c.valid || !c.hasGps()) continue;
    int tx = 0, ty = 0, px = 0, py = 0;
    geomath::latLonToTilePixel(c.latDeg(), c.lonDeg(), _map_zoom, tx, ty, px, py);
    int32_t wx = tx * 256 + px;
    int32_t wy = ty * 256 + py;
    int32_t sx = 154 + (wx - world_cx);
    int32_t sy = 72 + (wy - world_cy);
    if (sx < 8) sx = 8;
    if (sx > 218) sx = 218;
    if (sy < 18) sy = 18;
    if (sy > 124) sy = 124;

    for (size_t u = 0; u < used_count; ++u) {
      if (sy >= used_sy[u] - 12 && sy <= used_sy[u] + 12) {
        sy = used_sy[u] + 14;
        if (sy > 124) sy = 18 + int32_t(u * 14);
      }
    }
    if (used_count < 16) used_sy[used_count++] = sy;

    lv_obj_t* pin = lv_label_create(canvas);
    lv_obj_set_style_text_font(pin, &faro_font_12, 0);
    lv_obj_set_style_text_color(pin, c.role == NodeRole::Repeater ? p.success : p.warning, 0);
    char short_nm[12] = {0};
    strncpy(short_nm, c.name, 9);
    lv_label_set_text_fmt(pin, "*%s", short_nm);
    lv_obj_set_pos(pin, sx, sy);
  }

  // Overlay Range-Test SNR Heatmap samples if recorded
  for (size_t i = 0; i < _store.rangeSampleCount(); ++i) {
    const auto& rs = _store.rangeSamples()[i];
    if (!rs.valid) continue;
    int tx = 0, ty = 0, px = 0, py = 0;
    geomath::latLonToTilePixel(rs.lat_deg, rs.lon_deg, _map_zoom, tx, ty, px, py);
    int32_t sx = 154 + ((tx * 256 + px) - world_cx);
    int32_t sy = 72 + ((ty * 256 + py) - world_cy);
    if (sx < 6 || sx > 260 || sy < 18 || sy > 122) continue;
    int snr_db = int(rs.snr_q4) / 4;
    lv_obj_t* hm = lv_label_create(canvas);
    lv_obj_set_style_text_font(hm, &faro_font_12, 0);
    lv_obj_set_style_text_color(hm, snr_db >= 4 ? p.success : (snr_db >= -2 ? p.warning : p.danger), 0);
    lv_label_set_text_fmt(hm, "%+d", snr_db);
    lv_obj_set_pos(hm, sx, sy);
  }

  // Bottom map HUD bar: Coordinates + Pan [<][^][v][>] + [GPS] + [Trk] + Zoom [-][+]
  lv_obj_t* hud = lv_obj_create(parent);
  lv_obj_set_size(hud, 308, 22);
  lv_obj_set_style_bg_opa(hud, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hud, 0, 0);
  lv_obj_set_style_pad_all(hud, 0, 0);
  lv_obj_set_flex_flow(hud, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hud, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(hud, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* coord = lv_label_create(hud);
  lv_obj_set_style_text_font(coord, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(coord, p.text_primary, 0);
  lv_obj_add_flag(coord, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(coord, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_store.uiPrefs().coord_format = (self->_store.uiPrefs().coord_format + 1) % 3;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  uint8_t cfmt = _store.uiPrefs().coord_format % 3;
  if (cfmt == 1) {
    int32_t lv_e = 0, lv_n = 0;
    geomath::wgs84ToSwissLV95(clat, clon, lv_e, lv_n);
    lv_label_set_text_fmt(coord, "LV95 %d %d", int(lv_e), int(lv_n));
  } else if (cfmt == 2) {
    int u_z = 0; char u_b = 'T'; int32_t u_e = 0, u_n = 0;
    geomath::wgs84ToUtm(clat, clon, u_z, u_b, u_e, u_n);
    lv_label_set_text_fmt(coord, "%d%c %d %d", u_z, u_b, int(u_e), int(u_n));
  } else {
    lv_label_set_text_fmt(coord, "Z%d %.3fN %.3fE", _map_zoom, clat, clon);
  }

  const char* kMapBtns[7] = {"<", "^", "v", ">", g.valid ? "GPS" : "CH", "-", "+"};
  struct MapBtnCtx { App* app; int idx; };
  static MapBtnCtx s_mctx[7];
  for (int b = 0; b < 7; ++b) {
    lv_obj_t* mb = lv_button_create(hud);
    lv_obj_set_size(mb, b == 4 ? 28 : 19, 20);
    Theme::styleTouchButton(mb, (b == 5 || b == 6) ? p.accent : p.surface_alt,
                            (b == 5 || b == 6) ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_mctx[b] = {this, b};
    lv_obj_add_event_cb(mb, [](lv_event_t* e) {
      auto* ctx = static_cast<MapBtnCtx*>(lv_event_get_user_data(e));
      App* a = ctx->app;
      double step = 0.015 * (13 - (a->_map_zoom > 12 ? 12 : a->_map_zoom));
      if (step < 0.003) step = 0.003;
      if (ctx->idx == 0) a->_map_pan_lon_offset -= step;
      else if (ctx->idx == 1) a->_map_pan_lat_offset += step;
      else if (ctx->idx == 2) a->_map_pan_lat_offset -= step;
      else if (ctx->idx == 3) a->_map_pan_lon_offset += step;
      else if (ctx->idx == 4) {
        a->_map_pan_lat_offset = 0.0;
        a->_map_pan_lon_offset = 0.0;
      } else if (ctx->idx == 5) {
        a->_map_zoom = (a->_map_zoom <= 4) ? 17 : (a->_map_zoom - 1);
      } else if (ctx->idx == 6) {
        a->_map_zoom = (a->_map_zoom >= 17) ? 4 : (a->_map_zoom + 1);
      }
      a->renderBody();
    }, LV_EVENT_CLICKED, &s_mctx[b]);
    lv_obj_t* ml = lv_label_create(mb);
    lv_obj_set_style_text_font(ml, &faro_font_12, 0);
    lv_label_set_text(ml, kMapBtns[b]);
    lv_obj_center(ml);
  }
}


void App::buildSettingsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);
  const auto& r = _store.radio();
  const auto& st = _store.status();

  // Smart 3x3 Touch Category Hub when _settings_hub == true (9 thematic subpages, Node Name #1, SD Backup #9)
  if (_settings_hub) {
    static const char* kHubEN[9] = {
      "1. Node Name",   "2. LoRa Radio",  "3. Mesh Setup",
      "4. WiFi Setup",  "5. Bluetooth",   "6. GPS & Clock",
      "7. Lang/Screen", "8. Regions/CLI", "9. SD Backup"
    };
    static const uint8_t kHubTabMap[9] = {
      1, 2, 3,
      4, 5, 6,
      0, 7, 8
    };
    static const uint32_t kHubCols[9] = {
      0x007B83, 0x1E8E3E, 0x9334E6,
      0xE37400, 0x185ABC, 0x0D652D,
      0x1A73E8, 0xD93025, 0xB06000
    };
    struct HubCtx { App* app; uint8_t tab; };
    static HubCtx s_hctx[9];

    for (uint8_t row_idx = 0; row_idx < 3; ++row_idx) {
      lv_obj_t* hrow = lv_obj_create(parent);
      lv_obj_set_size(hrow, 308, 48);
      lv_obj_set_style_bg_opa(hrow, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(hrow, 0, 0);
      lv_obj_set_style_pad_all(hrow, 0, 0);
      lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_remove_flag(hrow, LV_OBJ_FLAG_SCROLLABLE);

      for (uint8_t col_idx = 0; col_idx < 3; ++col_idx) {
        uint8_t idx = row_idx * 3 + col_idx;
        lv_obj_t* hb = lv_button_create(hrow);
        lv_obj_set_size(hb, 99, 48);
        Theme::styleTouchButton(hb, lv_color_hex(kHubCols[idx]), lv_color_hex(0xFFFFFF));
        s_hctx[idx] = {this, kHubTabMap[idx]};
        lv_obj_add_event_cb(hb, [](lv_event_t* e) {
          auto* ctx = static_cast<HubCtx*>(lv_event_get_user_data(e));
          if (!ctx || !ctx->app) return;
          ctx->app->_settings_tab = ctx->tab;
          ctx->app->_settings_hub = false;
          ctx->app->rebuildChrome();
          ctx->app->updateStatusBar();
          ctx->app->renderBody();
        }, LV_EVENT_CLICKED, &s_hctx[idx]);
        lv_obj_t* hl = lv_label_create(hb);
        lv_obj_set_style_text_font(hl, &faro_font_12, 0);
        lv_label_set_text(hl, kHubEN[idx]);
        lv_obj_center(hl);
      }
    }
    return;
  }

  // Full-height Category Header Bar when inside a specific Settings subpage (_settings_hub == false):
  // [< Categories (96x28)]   Category Title   [Next > (74x28)]
  lv_obj_t* sbar = lv_obj_create(parent);
  lv_obj_set_size(sbar, 308, 30);
  lv_obj_set_style_bg_opa(sbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sbar, 0, 0);
  lv_obj_set_style_pad_all(sbar, 0, 0);
  lv_obj_set_flex_flow(sbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back_hub_btn = lv_button_create(sbar);
  lv_obj_set_size(back_hub_btn, 96, 28);
  Theme::styleTouchButton(back_hub_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(back_hub_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_settings_hub = true;
    self->rebuildChrome();
    self->updateStatusBar();
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* bhl = lv_label_create(back_hub_btn);
  lv_obj_set_style_text_font(bhl, &faro_font_12, 0);
  lv_label_set_text(bhl, "< Categories");
  lv_obj_center(bhl);

  static const char* kSTabsEN[9] = {
    "7/9 Lang & Screen", "1/9 Node Profile", "2/9 LoRa Radio",  "3/9 Mesh Setup",
    "4/9 WiFi Network",  "5/9 Bluetooth BLE","6/9 GPS & Clock", "8/9 Regions/CLI",
    "9/9 SD Import/Exp"
  };
  lv_obj_t* cat_lbl = lv_label_create(sbar);
  lv_obj_set_style_text_font(cat_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(cat_lbl, p.text_primary, 0);
  lv_label_set_text(cat_lbl, kSTabsEN[_settings_tab % 9]);

  lv_obj_t* next_tab_btn = lv_button_create(sbar);
  lv_obj_set_size(next_tab_btn, 74, 28);
  Theme::styleTouchButton(next_tab_btn, p.surface_alt, p.accent);
  lv_obj_add_event_cb(next_tab_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_settings_tab = (self->_settings_tab + 1) % 9;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* ntl = lv_label_create(next_tab_btn);
  lv_obj_set_style_text_font(ntl, &faro_font_12, 0);
  lv_label_set_text(ntl, "Next >");
  lv_obj_center(ntl);

  // SUBPAGE 0: Dedicated Language (EN, IT, DE, FR), Theme (Light/Dark) & Pocket Lock Screen
  if (_settings_tab == 0) {
    lv_obj_t* lcard = lv_obj_create(parent);
    lv_obj_set_size(lcard, 308, 170);
    Theme::styleCard(lcard, outdoor);
    lv_obj_set_style_pad_all(lcard, 6, 0);
    lv_obj_remove_flag(lcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* info = lv_label_create(lcard);
    lv_obj_set_pos(info, 0, 0);
    lv_obj_set_style_text_font(info, &faro_font_12, 0);
    lv_obj_set_style_text_color(info, p.text_secondary, 0);
    lv_label_set_text(info, "Select language (EN / IT / DE / FR) & screen:");

    struct LangCtx { App* app; Language lang; };
    static LangCtx s_lctx[4];
    static const Language kLangs[4] = {
      Language::EN, Language::IT, Language::DE, Language::FR
    };
    static const char* kLangLabels[4] = {
      "1. English (EN)", "2. Italiano (IT)", "3. Deutsch (DE)", "4. Français (FR)"
    };
    for (int i = 0; i < 4; ++i) {
      lv_obj_t* lb = lv_button_create(lcard);
      lv_obj_set_size(lb, 144, 38);
      lv_obj_set_pos(lb, (i % 2) * 150, 22 + (i / 2) * 44);
      bool sel = (lang == kLangs[i]);
      Theme::styleTouchButton(lb, sel ? p.accent : p.surface_alt,
                              sel ? lv_color_hex(0xFFFFFF) : p.text_primary);
      s_lctx[i] = {this, kLangs[i]};
      lv_obj_add_event_cb(lb, [](lv_event_t* e) {
        auto* ctx = static_cast<LangCtx*>(lv_event_get_user_data(e));
        if (ctx && ctx->app) {
          ctx->app->_store.uiPrefs().language = ctx->lang;
          ctx->app->_store.bumpVersion();
          ctx->app->showScreen(ctx->app->_current_screen);
        }
      }, LV_EVENT_CLICKED, &s_lctx[i]);
      lv_obj_t* ll = lv_label_create(lb);
      lv_obj_set_style_text_font(ll, &faro_font_12, 0);
      lv_label_set_text(ll, kLangLabels[i]);
      lv_obj_center(ll);
    }

    // Bottom row: [Theme: Light / Dark (94x38)] + [Dim: 60s/5m/OFF (94x38)] + [Lock Screen (94x38)]
    lv_obj_t* th_btn = lv_button_create(lcard);
    lv_obj_set_size(th_btn, 94, 38);
    lv_obj_set_pos(th_btn, 0, 114);
    Theme::styleTouchButton(th_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(th_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().outdoor_high_contrast = !self->_store.uiPrefs().outdoor_high_contrast;
      self->_store.bumpVersion();
      self->showScreen(self->_current_screen);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* tl = lv_label_create(th_btn);
    lv_obj_set_style_text_font(tl, &faro_font_12, 0);
    lv_label_set_text(tl, outdoor ? "Theme: Light" : "Theme: Dark");
    lv_obj_center(tl);

    lv_obj_t* dim_btn = lv_button_create(lcard);
    lv_obj_set_size(dim_btn, 94, 38);
    lv_obj_set_pos(dim_btn, 100, 114);
    Theme::styleTouchButton(dim_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(dim_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      uint16_t cur_t = self->_store.uiPrefs().screen_timeout_secs;
      if (cur_t == 60) self->_store.uiPrefs().screen_timeout_secs = 300;
      else if (cur_t == 300) self->_store.uiPrefs().screen_timeout_secs = 0;
      else self->_store.uiPrefs().screen_timeout_secs = 60;
      self->_store.bumpVersion();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* diml = lv_label_create(dim_btn);
    lv_obj_set_style_text_font(diml, &faro_font_12, 0);
    uint16_t t_sec = _store.uiPrefs().screen_timeout_secs;
    if (t_sec == 0) lv_label_set_text(diml, "Dim: OFF");
    else if (t_sec >= 300) lv_label_set_text(diml, "Dim: 5m");
    else lv_label_set_text_fmt(diml, "Dim: %us", unsigned(t_sec));
    lv_obj_center(diml);

    lv_obj_t* lck_btn = lv_button_create(lcard);
    lv_obj_set_size(lck_btn, 94, 38);
    lv_obj_set_pos(lck_btn, 200, 114);
    Theme::styleTouchButton(lck_btn, p.warning, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(lck_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_screen_locked = true;
      self->showScreen(ScreenId::Home);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* lckl = lv_label_create(lck_btn);
    lv_obj_set_style_text_font(lckl, &faro_font_12, 0);
    lv_label_set_text(lckl, "Lock Screen");
    lv_obj_center(lckl);
    return;
  }

  // SUBPAGE 1: Dedicated Node Profile, Identity, Advert Broadcast, Location Policy & QR
  if (_settings_tab == 1) {
    lv_obj_t* icard = lv_obj_create(parent);
    lv_obj_set_size(icard, 308, 170);
    Theme::styleCard(icard, outdoor);
    lv_obj_set_style_pad_all(icard, 6, 0);
    lv_obj_remove_flag(icard, LV_OBJ_FLAG_SCROLLABLE);

    _compose_ta = lv_textarea_create(icard);
    lv_obj_set_size(_compose_ta, 194, 32);
    lv_obj_set_pos(_compose_ta, 0, 0);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, st.node_name);
    lv_textarea_set_placeholder_text(_compose_ta, "Type Node Name...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sn_btn = lv_button_create(icard);
    lv_obj_set_size(sn_btn, 96, 32);
    lv_obj_set_pos(sn_btn, 198, 0);
    Theme::styleTouchButton(sn_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sn_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self && self->_compose_ta) {
        self->_actions.setNodeName(lv_textarea_get_text(self->_compose_ta));
        self->updateStatusBar();
        self->renderBody();
      }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* snl = lv_label_create(sn_btn);
    lv_obj_set_style_text_font(snl, &faro_font_12, 0);
    lv_label_set_text(snl, "Save Name");
    lv_obj_center(snl);

    char pub_hex[24] = {0};
    AppStore::bytesToHex(st.self_pubkey, 8, pub_hex, sizeof(pub_hex));
    lv_obj_t* klbl = lv_label_create(icard);
    lv_obj_set_pos(klbl, 0, 38);
    lv_obj_set_style_text_font(klbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(klbl, p.text_secondary, 0);
    lv_label_set_text_fmt(klbl, "Key: %s... | AutoAdv: %s", pub_hex, st.auto_advert ? "ON" : "OFF");

    // 2x2 Grid of Large MeshCore Node Profile Controls (144x38 px)
    lv_obj_t* zh_btn = lv_button_create(icard);
    lv_obj_set_size(zh_btn, 144, 38);
    lv_obj_set_pos(zh_btn, 0, 62);
    Theme::styleTouchButton(zh_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(zh_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.sendZeroHopAdvert();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* zhl = lv_label_create(zh_btn);
    lv_obj_set_style_text_font(zhl, &faro_font_12, 0);
    lv_label_set_text(zhl, "Send 0-Hop Advert");
    lv_obj_center(zhl);

    lv_obj_t* fl_btn = lv_button_create(icard);
    lv_obj_set_size(fl_btn, 144, 38);
    lv_obj_set_pos(fl_btn, 150, 62);
    Theme::styleTouchButton(fl_btn, p.warning, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(fl_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.sendFloodAdvert();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* fll = lv_label_create(fl_btn);
    lv_obj_set_style_text_font(fll, &faro_font_12, 0);
    lv_label_set_text(fll, "Send Flood Advert");
    lv_obj_center(fll);

    lv_obj_t* lc_btn = lv_button_create(icard);
    lv_obj_set_size(lc_btn, 144, 38);
    lv_obj_set_pos(lc_btn, 0, 108);
    Theme::styleTouchButton(lc_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(lc_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      uint8_t next = (self->_store.status().loc_policy + 1) % 3;
      self->_actions.setTelemetryPerms(self->_store.status().telemetry_perm, next);
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* lcl = lv_label_create(lc_btn);
    lv_obj_set_style_text_font(lcl, &faro_font_12, 0);
    static const char* kLocNames[3] = {"GPS Adv: OFF", "GPS Adv: Man", "GPS Adv: ON"};
    lv_label_set_text(lcl, kLocNames[st.loc_policy % 3]);
    lv_obj_center(lcl);

    lv_obj_t* qr_btn = lv_button_create(icard);
    lv_obj_set_size(qr_btn, 144, 38);
    lv_obj_set_pos(qr_btn, 150, 108);
    Theme::styleTouchButton(qr_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(qr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_show_my_qr_modal = true;
      self->showScreen(ScreenId::Home);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* qrl = lv_label_create(qr_btn);
    lv_obj_set_style_text_font(qrl, &faro_font_12, 0);
    lv_label_set_text(qrl, "Show My QR Code");
    lv_obj_center(qrl);
    return;
  }

  // SUBPAGE 2: Radio region, community presets & live Frequency/BW/SF/CR/TX controls (inside the region rules)
  if (_settings_tab == 2) {
    lv_obj_t* rcard = lv_obj_create(parent);
    lv_obj_set_size(rcard, 308, 170);
    Theme::styleCard(rcard, outdoor);
    lv_obj_set_style_pad_all(rcard, 6, 0);
    lv_obj_remove_flag(rcard, LV_OBJ_FLAG_SCROLLABLE);

    const RadioRegion& reg = _store.regionInfo();
    // Match active preset name or "Custom"
    const char* active_preset_name = "Custom";
    const int pi = RegionPolicy::findPreset(reg, r.freq_mhz, r.bw_khz, r.sf, r.cr);
    if (pi >= 0) {
      active_preset_name = reg.presets[pi].name;
      _preset_idx = size_t(pi);
    } else if (_preset_idx >= reg.preset_count) {
      _preset_idx = 0;
    }

    lv_obj_t* rl = lv_label_create(rcard);
    lv_obj_set_pos(rl, 0, 0);
    lv_obj_set_width(rl, 294);
    lv_label_set_long_mode(rl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(rl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(rl, p.accent, 0);
    lv_label_set_text_fmt(rl, "%.3f BW%.1f SF%u %ddBm %s%.0f duty%.0f%%",
                          double(r.freq_mhz), double(r.bw_khz), unsigned(r.sf), int(r.tx_power_dbm),
                          RegionPolicy::powerRefLabel(reg), double(RegionPolicy::radiatedDbm(reg, r.tx_power_dbm)),
                          double(RegionPolicy::dutyCyclePercent(r.airtime_factor)));
    if (_store.status().radio_notice[0]) {
      lv_label_set_text(rl, _store.status().radio_notice);
      lv_obj_set_style_text_color(rl, p.danger, 0);
    }

    struct RadCtx { App* app; int act; };
    static RadCtx s_rctx[13];
    auto bindRadBtn = [&](lv_obj_t* btn, int idx, int x, int y, int w, int h,
                          lv_color_t bg, lv_color_t fg, const char* text) {
      lv_obj_set_size(btn, w, h);
      lv_obj_set_pos(btn, x, y);
      Theme::styleTouchButton(btn, bg, fg);
      s_rctx[idx] = {this, idx};
      lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* ctx = static_cast<RadCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        RadioConfig cfg = a->_store.radio();
        const RadioRegion& rg = a->_store.regionInfo();
        int act = ctx->act;
        uint8_t preset_hash = 0;

        if (act == 12) {
          // [Region]: changes the frequency, so it needs a second tap within 5 s
          const RadioRegionId next = RegionPolicy::next(a->_store.region());
          const uint32_t now = lv_tick_get();
          if (a->_armed_action != 1200 || now - a->_armed_ms > 5000U) {
            a->_armed_action = 1200;
            a->_armed_ms = now;
            snprintf(a->_store.status().radio_notice, sizeof(a->_store.status().radio_notice),
                     "Tap Region again: %s", RegionPolicy::get(next).name);
            a->renderBody();
            return;
          }
          a->_armed_action = -1;
          if (!a->_actions.setRegion(next)) {
            snprintf(a->_store.status().radio_notice, sizeof(a->_store.status().radio_notice),
                     "Region not changed");
          }
          a->_preset_idx = 0;
          a->updateStatusBar();
          a->renderBody();
          return;
        } else if (act == 0 || act == 1 || act == 2) {
          // Preset <, Preset cycle, Preset >
          if (act == 0) {
            a->_preset_idx = (a->_preset_idx + rg.preset_count - 1) % rg.preset_count;
          } else {
            a->_preset_idx = (a->_preset_idx + 1) % rg.preset_count;
          }
          const auto& pr = rg.presets[a->_preset_idx];
          cfg.freq_mhz = pr.freq_mhz;
          cfg.bw_khz = pr.bw_khz;
          cfg.sf = pr.sf;
          cfg.cr = pr.cr;
          cfg.client_repeat = cfg.client_repeat && RegionPolicy::isRepeatFrequency(rg, pr.freq_mhz);
          preset_hash = pr.path_hash_bytes;
        } else if (act == 3 || act == 5) {
          // [Freq -/+]: 25 kHz steps; the whole channel must stay inside the region's band
          float next_f = cfg.freq_mhz + (act == 5 ? 0.025f : -0.025f);
          if (!RegionPolicy::channelInRegion(rg, next_f, cfg.bw_khz)) return;
          cfg.freq_mhz = next_f;
          cfg.client_repeat = cfg.client_repeat && RegionPolicy::isRepeatFrequency(rg, next_f);
        } else if (act == 4) {
          // [Freq]: cycle the region's preset frequencies that fit the current bandwidth
          float freqs[12];
          size_t nf = 0;
          for (uint8_t k = 0; k < rg.preset_count && nf < 12; ++k) {
            bool dup = false;
            for (size_t j = 0; j < nf; ++j) dup = dup || fabsf(freqs[j] - rg.presets[k].freq_mhz) < 0.002f;
            if (!dup) freqs[nf++] = rg.presets[k].freq_mhz;
          }
          int cur_k = -1;
          for (size_t k = 0; k < nf; ++k) {
            if (fabsf(cfg.freq_mhz - freqs[k]) < 0.002f) cur_k = int(k);
          }
          for (size_t step = 1; step <= nf; ++step) {
            float f = freqs[(size_t(cur_k + int(nf)) + step) % nf];
            if (RegionPolicy::channelInRegion(rg, f, cfg.bw_khz)) {
              cfg.freq_mhz = f;
              break;
            }
          }
          cfg.client_repeat = cfg.client_repeat && RegionPolicy::isRepeatFrequency(rg, cfg.freq_mhz);
        } else if (act == 6) {
          // [BW]: next supported bandwidth that still fits the band at this frequency
          static const float kBws[6] = {62.5f, 125.0f, 250.0f, 500.0f, 31.25f, 41.7f};
          int cur_k = -1;
          for (int k = 0; k < 6; ++k) {
            if (fabsf(cfg.bw_khz - kBws[k]) < 0.5f) cur_k = k;
          }
          for (int step = 1; step <= 6; ++step) {
            float bw = kBws[(cur_k + step + 6) % 6];
            if (RadioConfig::isValidSx1262Bandwidth(bw) && RegionPolicy::channelInRegion(rg, cfg.freq_mhz, bw)) {
              cfg.bw_khz = bw;
              break;
            }
          }
        } else if (act == 7) {
          // [SF]: cycle 7..12
          cfg.sf = (cfg.sf >= 12 || cfg.sf < 7) ? 7 : uint8_t(cfg.sf + 1);
        } else if (act == 8) {
          // [CR]: cycle 5..8
          cfg.cr = (cfg.cr >= 8 || cfg.cr < 5) ? 5 : uint8_t(cfg.cr + 1);
        } else if (act == 9) {
          // [TX Power]: next step, capped by the region's e.r.p./e.i.r.p. limit
          cfg.tx_power_dbm = nextTxPower(cfg.tx_power_dbm, RegionPolicy::maxConductedDbm(rg));
        } else if (act == 10) {
          // [RPT]: toggle client repeat
          a->_actions.setRepeatEnabled(!cfg.client_repeat);
          a->updateStatusBar();
          a->renderBody();
          return;
        } else if (act == 11) {
          // [RX Boost]: toggle SX1262 boosted gain
          cfg.rx_boosted_gain = !cfg.rx_boosted_gain;
        }

        if (a->_actions.setRadioConfig(cfg) && preset_hash != 0) a->_actions.setPathHashMode(preset_hash);
        a->updateStatusBar();
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_rctx[idx]);

      lv_obj_t* l = lv_label_create(btn);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, text);
      lv_obj_center(l);
    };

    // Row 1 (y=20): [Region] [<] [Preset Name] [>]
    char rg_lbl[24], pr_lbl[40];
    snprintf(rg_lbl, sizeof(rg_lbl), "%s", reg.code);
    snprintf(pr_lbl, sizeof(pr_lbl), "%s", active_preset_name);
    bindRadBtn(lv_button_create(rcard), 12, 0,   20, 90,  32, p.warning, lv_color_hex(0xFFFFFF), rg_lbl);
    bindRadBtn(lv_button_create(rcard), 0,  94,  20, 34,  32, p.surface_alt, p.text_primary, "<");
    bindRadBtn(lv_button_create(rcard), 1,  132, 20, 124, 32, p.accent, lv_color_hex(0xFFFFFF), pr_lbl);
    bindRadBtn(lv_button_create(rcard), 2,  260, 20, 34,  32, p.surface_alt, p.text_primary, ">");

    // Row 2 (y=56): [Freq -] [MHz] [Freq +]
    char fq_lbl[32];
    snprintf(fq_lbl, sizeof(fq_lbl), "%.3f MHz", double(r.freq_mhz));
    bindRadBtn(lv_button_create(rcard), 3, 0,   56, 68,  32, p.surface_alt, p.text_primary, "Freq -");
    bindRadBtn(lv_button_create(rcard), 4, 72,  56, 150, 32, p.success, lv_color_hex(0xFFFFFF), fq_lbl);
    bindRadBtn(lv_button_create(rcard), 5, 226, 56, 68,  32, p.surface_alt, p.text_primary, "Freq +");

    // Row 3 (y=92): [BW: ...] [SF: ...] [CR: ...]
    char bw_lbl[24], sf_lbl[24], cr_lbl[24];
    snprintf(bw_lbl, sizeof(bw_lbl), "BW: %.1fk", double(r.bw_khz));
    snprintf(sf_lbl, sizeof(sf_lbl), "SF: %u", unsigned(r.sf));
    snprintf(cr_lbl, sizeof(cr_lbl), "CR: 4/%u", unsigned(r.cr));
    bindRadBtn(lv_button_create(rcard), 6, 0,   92, 94, 32, p.surface_alt, p.accent, bw_lbl);
    bindRadBtn(lv_button_create(rcard), 7, 100, 92, 94, 32, p.surface_alt, p.accent, sf_lbl);
    bindRadBtn(lv_button_create(rcard), 8, 200, 92, 94, 32, p.surface_alt, p.accent, cr_lbl);

    // Row 4 (y=128): [TX: ...dBm] [RPT: ON/OFF] [Boost: ON/OFF]
    char tx_lbl[24], rpt_lbl[24], bst_lbl[24];
    snprintf(tx_lbl, sizeof(tx_lbl), "TX: %ddBm", int(r.tx_power_dbm));
    snprintf(rpt_lbl, sizeof(rpt_lbl), "RPT: %s", r.client_repeat ? "ON" : "OFF");
    snprintf(bst_lbl, sizeof(bst_lbl), "Boost: %s", r.rx_boosted_gain ? "ON" : "OFF");
    bindRadBtn(lv_button_create(rcard), 9,  0,   128, 94, 30, p.surface_alt, p.text_primary, tx_lbl);
    bindRadBtn(lv_button_create(rcard), 10, 100, 128, 94, 30,
               r.client_repeat ? p.warning : p.surface_alt,
               r.client_repeat ? lv_color_hex(0xFFFFFF) : p.text_primary, rpt_lbl);
    bindRadBtn(lv_button_create(rcard), 11, 200, 128, 94, 30,
               r.rx_boosted_gain ? p.success : p.surface_alt,
               r.rx_boosted_gain ? lv_color_hex(0xFFFFFF) : p.text_primary, bst_lbl);
    return;
  }

  // SUBPAGE 3 (3/9): MeshCore Android App Settings (Contacts Auto-Add, Telemetry Base/Loc/Env, Path Hash, Multi-ACK, RF Tuning)
  if (_settings_tab == 3) {
    lv_obj_t* ocard = lv_obj_create(parent);
    lv_obj_set_size(ocard, 308, 170);
    Theme::styleCard(ocard, outdoor);
    lv_obj_set_style_pad_all(ocard, 6, 0);
    lv_obj_remove_flag(ocard, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kTelModes[3] = {"Deny", "Cnts", "All"};
    lv_obj_t* olbl = lv_label_create(ocard);
    lv_obj_set_pos(olbl, 0, 0);
    lv_obj_set_style_text_font(olbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(olbl, p.text_primary, 0);
    if (!_mesh_tuning_page) {
      lv_label_set_text_fmt(olbl,
                            "Contacts: %s (MaxHops: %u)\n"
                            "Telem Base:%s | Loc:%s | Env:%s\n"
                            "Path Hash: %uB | Multi-ACK: %ux",
                            st.auto_add_contacts ? "Auto-Add" : "Manual",
                            unsigned(st.auto_add_max_hops),
                            kTelModes[st.telemetry_perm % 3],
                            kTelModes[st.telemetry_loc_perm % 3],
                            kTelModes[st.telemetry_env_perm % 3],
                            unsigned(st.path_hash_bytes),
                            unsigned(st.multi_acks));
    } else {
      lv_label_set_text_fmt(olbl,
                            "Multi-ACK: %ux | CAD: %s | Hash: %uB\n"
                            "RX Delay: %.2fs | TX Delay: %.2f\n"
                            "Interf: %udB | AGC Reset: %us",
                            unsigned(st.multi_acks),
                            st.cad_enabled ? "ON" : "OFF",
                            unsigned(st.path_hash_bytes),
                            double(st.rx_delay), double(st.tx_delay_factor),
                            unsigned(st.interference_db), unsigned(st.agc_reset_sec));
    }

    char b0[24], b1[24], b2[24], b3[24], b4[24], b5[24];
    if (!_mesh_tuning_page) {
      snprintf(b0, sizeof(b0), "Add:%s", st.auto_add_contacts ? "Auto" : "Man");
      if (st.auto_add_max_hops == 0) snprintf(b1, sizeof(b1), "Hops: All");
      else snprintf(b1, sizeof(b1), "Hops: %uh", unsigned(st.auto_add_max_hops));
      snprintf(b2, sizeof(b2), "TelB:%s", kTelModes[st.telemetry_perm % 3]);
      snprintf(b3, sizeof(b3), "TelL:%s", kTelModes[st.telemetry_loc_perm % 3]);
      snprintf(b4, sizeof(b4), "Hash: %uB", unsigned(st.path_hash_bytes));
      snprintf(b5, sizeof(b5), "RF Tune >");
    } else {
      snprintf(b0, sizeof(b0), "ACK: %ux", unsigned(st.multi_acks));
      snprintf(b1, sizeof(b1), "CAD: %s", st.cad_enabled ? "ON" : "OFF");
      snprintf(b2, sizeof(b2), "RXD:%.2fs", double(st.rx_delay));
      snprintf(b3, sizeof(b3), "TXD:%.2f", double(st.tx_delay_factor));
      snprintf(b4, sizeof(b4), "Intf:%udB", unsigned(st.interference_db));
      snprintf(b5, sizeof(b5), "< Cnt/Tel");
    }
    const char* kOptLabels[6] = {b0, b1, b2, b3, b4, b5};

    struct OptCtx { App* app; int idx; };
    static OptCtx s_octx[6];
    for (int i = 0; i < 6; ++i) {
      lv_obj_t* b = lv_button_create(ocard);
      lv_obj_set_size(b, 94, 38);
      lv_obj_set_pos(b, (i % 3) * 100, 62 + (i / 3) * 46);
      Theme::styleTouchButton(b, i == 5 ? p.accent : p.surface_alt,
                              i == 5 ? lv_color_hex(0xFFFFFF) : p.accent);
      s_octx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<OptCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        auto& s = a->_store.status();
        if (!a->_mesh_tuning_page) {
          if (ctx->idx == 0) {
            a->_actions.setAutoAddConfig(!s.auto_add_contacts, s.auto_add_max_hops);
          } else if (ctx->idx == 1) {
            static const uint8_t kHops[5] = {0, 1, 2, 3, 5};
            uint8_t next_hp = 0;
            for (int k = 0; k < 5; ++k) {
              if (s.auto_add_max_hops == kHops[k]) {
                next_hp = kHops[(k + 1) % 5];
                break;
              }
            }
            a->_actions.setAutoAddConfig(s.auto_add_contacts, next_hp);
          } else if (ctx->idx == 2) {
            a->_actions.setTelemetryPerms((s.telemetry_perm + 1) % 3, s.loc_policy);
          } else if (ctx->idx == 3) {
            s.telemetry_loc_perm = (s.telemetry_loc_perm + 1) % 3;
            s.telemetry_env_perm = s.telemetry_loc_perm;
            a->_actions.setTelemetryPerms(s.telemetry_perm, s.loc_policy);
          } else if (ctx->idx == 4) {
            uint8_t next_h = (s.path_hash_bytes >= 3) ? 1 : uint8_t(s.path_hash_bytes + 1);
            a->_actions.setPathHashMode(next_h);
          } else if (ctx->idx == 5) {
            a->_mesh_tuning_page = true;
          }
        } else {
          if (ctx->idx == 0) {
            s.multi_acks = (s.multi_acks + 1) % 3;
            a->_actions.setTuningParams(s.rx_delay, s.interference_db, s.agc_reset_sec, s.multi_acks > 0);
          } else if (ctx->idx == 1) {
            s.cad_enabled = !s.cad_enabled;
            a->_actions.setTuningParams(s.rx_delay, s.interference_db, s.agc_reset_sec, s.multi_acks > 0);
          } else if (ctx->idx == 2) {
            float next_d = (s.rx_delay >= 0.5f) ? 0.0f : (s.rx_delay + 0.25f);
            a->_actions.setTuningParams(next_d, s.interference_db, s.agc_reset_sec, s.multi_acks > 0);
          } else if (ctx->idx == 3) {
            s.tx_delay_factor = (s.tx_delay_factor >= 0.9f) ? 0.2f : (s.tx_delay_factor + 0.3f);
            a->_actions.setTuningParams(s.rx_delay, s.interference_db, s.agc_reset_sec, s.multi_acks > 0);
          } else if (ctx->idx == 4) {
            uint8_t next_i = (s.interference_db >= 20) ? 10 : uint8_t(s.interference_db + 4);
            uint8_t next_agc = (s.agc_reset_sec >= 60) ? 16 : uint8_t(s.agc_reset_sec + 16);
            a->_actions.setTuningParams(s.rx_delay, next_i, next_agc, s.multi_acks > 0);
          } else if (ctx->idx == 5) {
            a->_mesh_tuning_page = false;
          }
        }
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_octx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, kOptLabels[i]);
      lv_obj_center(l);
    }
    return;
  }

  // SUBPAGE 4 (5/8): Dedicated WiFi Network Station Setup (SSID + Password -> DHCP IP)
  if (_settings_tab == 4) {
    lv_obj_t* wcard = lv_obj_create(parent);
    lv_obj_set_size(wcard, 308, 170);
    Theme::styleCard(wcard, outdoor);
    lv_obj_set_style_pad_all(wcard, 6, 0);
    lv_obj_remove_flag(wcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wlbl = lv_label_create(wcard);
    lv_obj_set_pos(wlbl, 0, 0);
    lv_obj_set_style_text_font(wlbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(wlbl, p.text_primary, 0);
    lv_label_set_text_fmt(wlbl,
                          "WiFi: %s | Status: %s\n"
                          "SSID: %s | Pwd: %s\n"
                          "IP (DHCP): %s",
                          st.wifi_enabled ? "ON" : "OFF",
                          st.wifi_connected ? "CONNECTED" : "DISCONNECTED",
                          st.wifi_ssid[0] ? st.wifi_ssid : "(not set)",
                          st.wifi_pwd[0] ? "********" : "(empty)",
                          st.wifi_connected ? st.wifi_ip : "(auto via DHCP)");

    _compose_ta = lv_textarea_create(wcard);
    lv_obj_set_size(_compose_ta, 198, 28);
    lv_obj_set_pos(_compose_ta, 0, 48);
    lv_textarea_set_one_line(_compose_ta, true);
    if (_wifi_edit_pwd) {
      // never put the stored password back on screen
      lv_textarea_set_password_mode(_compose_ta, true);
      lv_textarea_set_placeholder_text(_compose_ta, "New WiFi password (hidden)...");
    } else {
      lv_textarea_set_text(_compose_ta, st.wifi_ssid);
      lv_textarea_set_placeholder_text(_compose_ta, "Type WiFi SSID (Name)...");
    }
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sv_btn = lv_button_create(wcard);
    lv_obj_set_size(sv_btn, 92, 28);
    lv_obj_set_pos(sv_btn, 202, 48);
    Theme::styleTouchButton(sv_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sv_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self || !self->_compose_ta) return;
      const char* txt = lv_textarea_get_text(self->_compose_ta);
      if (self->_wifi_edit_pwd) {
        if (!txt || !txt[0]) return;  // empty field keeps the current password
        self->_actions.setWifiConfig(true, self->_store.status().wifi_ssid, txt);
        lv_textarea_set_text(self->_compose_ta, "");
      } else {
        self->_actions.setWifiConfig(true, txt ? txt : "", self->_store.status().wifi_pwd);
      }
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* svl = lv_label_create(sv_btn);
    lv_obj_set_style_text_font(svl, &faro_font_12, 0);
    lv_label_set_text(svl, _wifi_edit_pwd ? "Save Pwd" : "Save SSID");
    lv_obj_center(svl);

    // 2x2 Grid of Large Touch Buttons (144x34 px)
    lv_obj_t* fld_btn = lv_button_create(wcard);
    lv_obj_set_size(fld_btn, 144, 34);
    lv_obj_set_pos(fld_btn, 0, 84);
    Theme::styleTouchButton(fld_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(fld_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self->_compose_ta) {
        const char* cur = lv_textarea_get_text(self->_compose_ta);
        if (self->_wifi_edit_pwd) {
          strncpy(self->_store.status().wifi_pwd, cur ? cur : "", sizeof(self->_store.status().wifi_pwd) - 1);
        } else {
          strncpy(self->_store.status().wifi_ssid, cur ? cur : "", sizeof(self->_store.status().wifi_ssid) - 1);
        }
      }
      self->_wifi_edit_pwd = !self->_wifi_edit_pwd;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* fldl = lv_label_create(fld_btn);
    lv_obj_set_style_text_font(fldl, &faro_font_12, 0);
    lv_label_set_text(fldl, _wifi_edit_pwd ? "Field: Password" : "Field: WiFi SSID");
    lv_obj_center(fldl);

    lv_obj_t* join_btn = lv_button_create(wcard);
    lv_obj_set_size(join_btn, 144, 34);
    lv_obj_set_pos(join_btn, 150, 84);
    Theme::styleTouchButton(join_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(join_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self->_compose_ta) {
        const char* cur = lv_textarea_get_text(self->_compose_ta);
        if (self->_wifi_edit_pwd) {
          self->_actions.setWifiConfig(true, self->_store.status().wifi_ssid, cur ? cur : "");
        } else {
          self->_actions.setWifiConfig(true, cur ? cur : "", self->_store.status().wifi_pwd);
        }
      } else {
        self->_actions.setWifiConfig(true, self->_store.status().wifi_ssid, self->_store.status().wifi_pwd);
      }
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* joinl = lv_label_create(join_btn);
    lv_obj_set_style_text_font(joinl, &faro_font_12, 0);
    lv_label_set_text(joinl, "Connect WiFi");
    lv_obj_center(joinl);

    lv_obj_t* pwr_btn = lv_button_create(wcard);
    lv_obj_set_size(pwr_btn, 144, 34);
    lv_obj_set_pos(pwr_btn, 0, 122);
    Theme::styleTouchButton(pwr_btn, st.wifi_enabled ? p.warning : p.surface_alt,
                            st.wifi_enabled ? lv_color_hex(0xFFFFFF) : p.text_primary);
    lv_obj_add_event_cb(pwr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      bool next = !self->_store.status().wifi_enabled;
      self->_actions.setWifiConfig(next, self->_store.status().wifi_ssid, self->_store.status().wifi_pwd);
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* pwrl = lv_label_create(pwr_btn);
    lv_obj_set_style_text_font(pwrl, &faro_font_12, 0);
    lv_label_set_text_fmt(pwrl, "WiFi: %s", st.wifi_enabled ? "ON" : "OFF");
    lv_obj_center(pwrl);

    lv_obj_t* clr_btn = lv_button_create(wcard);
    lv_obj_set_size(clr_btn, 144, 34);
    lv_obj_set_pos(clr_btn, 150, 122);
    Theme::styleTouchButton(clr_btn, p.surface_alt, p.danger);
    lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.status().wifi_ssid[0] = '\0';
      self->_store.status().wifi_pwd[0] = '\0';
      self->_actions.setWifiConfig(false, "", "");
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* clrl = lv_label_create(clr_btn);
    lv_obj_set_style_text_font(clrl, &faro_font_12, 0);
    lv_label_set_text(clrl, "Forget Network");
    lv_obj_center(clrl);
    return;
  }

  // SUBPAGE 5 (6/8): Dedicated Bluetooth BLE Pairing & Notification Settings
  if (_settings_tab == 5) {
    lv_obj_t* bcard = lv_obj_create(parent);
    lv_obj_set_size(bcard, 308, 170);
    Theme::styleCard(bcard, outdoor);
    lv_obj_set_style_pad_all(bcard, 6, 0);
    lv_obj_remove_flag(bcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* blbl = lv_label_create(bcard);
    lv_obj_set_pos(blbl, 0, 0);
    lv_obj_set_style_text_font(blbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(blbl, p.text_primary, 0);
    lv_label_set_text_fmt(blbl,
                          "Bluetooth BLE: ACTIVE (%s)\n"
                          "Pairing PIN: %06u\n"
                          "BLE Device Name: %s",
                          st.ble_connected ? "CONNECTED" : "ADVERTISING",
                          unsigned(st.ble_pin), st.node_name);

    char pin_buf[16];
    snprintf(pin_buf, sizeof(pin_buf), "%06u", unsigned(st.ble_pin));
    _compose_ta = lv_textarea_create(bcard);
    lv_obj_set_size(_compose_ta, 194, 28);
    lv_obj_set_pos(_compose_ta, 0, 48);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_text(_compose_ta, pin_buf);
    lv_textarea_set_placeholder_text(_compose_ta, "6-digit BLE PIN...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* sp_btn = lv_button_create(bcard);
    lv_obj_set_size(sp_btn, 96, 28);
    lv_obj_set_pos(sp_btn, 198, 48);
    Theme::styleTouchButton(sp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self || !self->_compose_ta) return;
      uint32_t parsed = uint32_t(atoi(lv_textarea_get_text(self->_compose_ta)));
      if (parsed >= 100000 && parsed <= 999999) {
        self->_actions.setBlePin(parsed);
        self->renderBody();
      }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* spl = lv_label_create(sp_btn);
    lv_obj_set_style_text_font(spl, &faro_font_12, 0);
    lv_label_set_text(spl, "Save PIN");
    lv_obj_center(spl);

    // 2x2 Grid of Large Bluetooth & Alert Buttons (144x34 px)
    lv_obj_t* p1_btn = lv_button_create(bcard);
    lv_obj_set_size(p1_btn, 144, 34);
    lv_obj_set_pos(p1_btn, 0, 84);
    Theme::styleTouchButton(p1_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(p1_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.setBlePin(randomBlePin());
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* p1l = lv_label_create(p1_btn);
    lv_obj_set_style_text_font(p1l, &faro_font_12, 0);
    lv_label_set_text(p1l, "New random PIN");
    lv_obj_center(p1l);

    lv_obj_t* p2_btn = lv_button_create(bcard);
    lv_obj_set_size(p2_btn, 144, 34);
    lv_obj_set_pos(p2_btn, 150, 84);
    Theme::styleTouchButton(p2_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(p2_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.setBlePin(0);  // 0 = new random PIN on every boot, shown on screen
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* p2l = lv_label_create(p2_btn);
    lv_obj_set_style_text_font(p2l, &faro_font_12, 0);
    lv_label_set_text(p2l, "Random each boot");
    lv_obj_center(p2l);

    lv_obj_t* qm_btn = lv_button_create(bcard);
    lv_obj_set_size(qm_btn, 144, 34);
    lv_obj_set_pos(qm_btn, 0, 122);
    Theme::styleTouchButton(qm_btn, _store.uiPrefs().quiet_mode ? p.warning : p.surface_alt,
                            _store.uiPrefs().quiet_mode ? lv_color_hex(0xFFFFFF) : p.text_primary);
    lv_obj_add_event_cb(qm_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().quiet_mode = !self->_store.uiPrefs().quiet_mode;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* qml = lv_label_create(qm_btn);
    lv_obj_set_style_text_font(qml, &faro_font_12, 0);
    lv_label_set_text_fmt(qml, "Quiet Mode: %s", _store.uiPrefs().quiet_mode ? "ON" : "OFF");
    lv_obj_center(qml);

    lv_obj_t* adv_btn = lv_button_create(bcard);
    lv_obj_set_size(adv_btn, 144, 34);
    lv_obj_set_pos(adv_btn, 150, 122);
    Theme::styleTouchButton(adv_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(adv_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.sendZeroHopAdvert();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* advl = lv_label_create(adv_btn);
    lv_obj_set_style_text_font(advl, &faro_font_12, 0);
    lv_label_set_text(advl, "Send 0-Hop Advert");
    lv_obj_center(advl);
    return;
  }

  // SUBPAGE 6 (7/8): Dedicated GPS (L76K GNSS) & Clock / Timezone Settings
  if (_settings_tab == 6) {
    lv_obj_t* gcard = lv_obj_create(parent);
    lv_obj_set_size(gcard, 308, 170);
    Theme::styleCard(gcard, outdoor);
    lv_obj_set_style_pad_all(gcard, 6, 0);
    lv_obj_remove_flag(gcard, LV_OBJ_FLAG_SCROLLABLE);

    const auto& g = _store.gps();
    const auto& trk = _store.gpsTrack();
    static const char* kFmtNames[3] = {"WGS84", "LV95", "UTM"};

    lv_obj_t* glbl = lv_label_create(gcard);
    lv_obj_set_pos(glbl, 0, 0);
    lv_obj_set_style_text_font(glbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(glbl, p.text_primary, 0);
    char gps_pos[48] = "-- (no fix)";
    if (g.valid) snprintf(gps_pos, sizeof(gps_pos), "%.4f, %.4f (%.0fm)", g.lat_deg, g.lon_deg, g.alt_m);
    lv_label_set_text_fmt(glbl,
                          "GPS: %s | Fix: %s | NMEA: %u\n"
                          "Pos: %s\n"
                          "Baud: %u | Zone: UTC%+d | Fmt: %s",
                          g.enabled ? "ON" : "OFF",
                          g.valid ? "3D LOCK" : "SEARCHING",
                          unsigned(g.nmea_sentences),
                          gps_pos,
                          unsigned(g.baud_rate), int(st.tz_offset_hours),
                          kFmtNames[_store.uiPrefs().coord_format % 3]);

    // 3x2 Grid of 6 Large GPS & Clock Buttons (94x38 px — zero text fields!)
    char gb0[24], gb1[24], gb2[24], gb3[24], gb4[24], gb5[24];
    snprintf(gb0, sizeof(gb0), "GPS: %s", g.enabled ? "ON" : "OFF");
    snprintf(gb1, sizeof(gb1), "Baud:%uk", unsigned(g.baud_rate / 1000));
    snprintf(gb2, sizeof(gb2), "AdvGPS:%s", st.loc_policy == 2 ? "ON" : "OFF");
    snprintf(gb3, sizeof(gb3), "Fmt:%s", kFmtNames[_store.uiPrefs().coord_format % 3]);
    snprintf(gb4, sizeof(gb4), "Zone:UTC%+d", int(st.tz_offset_hours));
    snprintf(gb5, sizeof(gb5), "GPX:%s", trk.recording ? "REC" : "STOP");
    const char* kGBtns[6] = {gb0, gb1, gb2, gb3, gb4, gb5};

    struct GCtx { App* app; int idx; };
    static GCtx s_gctx[6];
    for (int i = 0; i < 6; ++i) {
      lv_obj_t* b = lv_button_create(gcard);
      lv_obj_set_size(b, 94, 38);
      lv_obj_set_pos(b, (i % 3) * 100, 62 + (i / 3) * 46);
      bool highlight = (i == 0 && g.enabled) || (i == 5 && trk.recording);
      Theme::styleTouchButton(b, highlight ? p.success : p.surface_alt,
                              highlight ? lv_color_hex(0xFFFFFF) : p.accent);
      s_gctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<GCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        auto& gf = a->_store.gps();
        auto& s = a->_store.status();
        if (ctx->idx == 0) {
          bool next_en = !gf.enabled;
          a->_actions.setGpsConfig(next_en, gf.baud_rate, 10, s.loc_policy == 2);
        } else if (ctx->idx == 1) {
          uint32_t next_b = (gf.baud_rate == 38400) ? 9600 : 38400;
          a->_actions.setGpsConfig(gf.enabled, next_b, 10, s.loc_policy == 2);
        } else if (ctx->idx == 2) {
          uint8_t next_loc = (s.loc_policy == 2) ? 0 : 2;
          a->_actions.setTelemetryPerms(s.telemetry_perm, next_loc);
        } else if (ctx->idx == 3) {
          a->_store.uiPrefs().coord_format = (a->_store.uiPrefs().coord_format + 1) % 3;
        } else if (ctx->idx == 4) {
          s.tz_offset_hours = (s.tz_offset_hours >= 4) ? -2 : int8_t(s.tz_offset_hours + 1);
          a->_actions.syncDeviceTime(0);
        } else if (ctx->idx == 5) {
          a->_store.gpsTrack().recording = !a->_store.gpsTrack().recording;
        }
        a->updateStatusBar();
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_gctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, kGBtns[i]);
      lv_obj_center(l);
    }
    return;
  }

  // SUBPAGE 8 (9/9): Dedicated MicroSD Card Settings & Keys Import / Export (with Overwrite)
  if (_settings_tab == 8) {
    lv_obj_t* sdcard = lv_obj_create(parent);
    lv_obj_set_size(sdcard, 308, 170);
    Theme::styleCard(sdcard, outdoor);
    lv_obj_set_style_pad_all(sdcard, 6, 0);
    lv_obj_remove_flag(sdcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* sdlbl = lv_label_create(sdcard);
    lv_obj_set_pos(sdlbl, 0, 0);
    lv_obj_set_width(sdcard, 308);
    lv_obj_set_style_text_font(sdlbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(sdlbl, p.text_primary, 0);
    lv_label_set_text_fmt(sdlbl,
                          "SD Card: %s (%u/%u MB) | Overwrite: ON\n"
                          "Files: /faro/settings.bin (no Wi-Fi password)\n"
                          "%s",
                          st.sd_mounted ? "MOUNTED" : "AUTO-MOUNT",
                          unsigned(st.sd_used_mb), unsigned(st.sd_total_mb),
                          st.sd_backup_status);

    static const char* kSdBtns[6] = {
      "Export Settings -> SD", "Import Settings <- SD",
      "Export Keys -> SD",     "Import Keys <- SD",
      "Export ALL -> SD",      "Import ALL <- SD"
    };
    static const uint32_t kSdCols[6] = {
      0x1E8E3E, 0x1A73E8,
      0xE37400, 0x9334E6,
      0x007B83, 0xD93025
    };
    struct SdCtx { App* app; int idx; };
    static SdCtx s_sdctx[6];
    for (int i = 0; i < 6; ++i) {
      lv_obj_t* b = lv_button_create(sdcard);
      lv_obj_set_size(b, 144, 32);
      lv_obj_set_pos(b, (i % 2) * 150, 50 + (i / 2) * 36);
      Theme::styleTouchButton(b, lv_color_hex(kSdCols[i]), lv_color_hex(0xFFFFFF));
      s_sdctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<SdCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        auto& s = a->_store.status();
        if (ctx->idx == 0) {
          a->_actions.exportSettingsToSd(true);
        } else if (ctx->idx == 1) {
          a->_actions.importSettingsFromSd();
          a->rebuildChrome();
          a->updateStatusBar();
        } else if (ctx->idx == 2 || ctx->idx == 4) {
          if (!a->confirmTap(ctx->idx, "PRIVATE KEY in plain text on SD! Tap again (5 s) to confirm.")) {
            a->renderBody();
            return;
          }
          bool ok_s = (ctx->idx == 4) ? a->_actions.exportSettingsToSd(true) : true;
          bool ok_k = a->_actions.exportIdentityToSd();
          snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                   (ok_s && ok_k) ? "OK: /faro/identity.bak written. Keep the card safe, delete when done."
                                  : "ERR: Export to MicroSD failed.");
        } else if (ctx->idx == 3) {
          if (!a->confirmTap(ctx->idx, "Replace this node's identity from SD? Tap again (5 s).")) {
            a->renderBody();
            return;
          }
          if (a->_actions.importIdentityHex("SD")) {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "OK: Identity imported. Delete the key file from the card.");
          } else {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "ERR: /faro/identity.bak or prv_key.txt not found/invalid.");
          }
        } else if (ctx->idx == 5) {
          if (!a->confirmTap(ctx->idx, "Replace settings AND identity from SD? Tap again (5 s).")) {
            a->renderBody();
            return;
          }
          bool ok_s = a->_actions.importSettingsFromSd();
          bool ok_k = a->_actions.importIdentityHex("SD");
          if (ok_s || ok_k) {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "OK: Restored ALL (%s%s) from SD!",
                     ok_s ? "Settings " : "", ok_k ? "+ Keys" : "");
          }
          a->rebuildChrome();
          a->updateStatusBar();
        }
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_sdctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, kSdBtns[i]);
      lv_obj_center(l);
    }
    return;
  }

  // SUBPAGE 7 (8/9): Companion CLI Console, Regions / Off-Grid Mode, RF Diagnostics, Reboot & Wizard
  lv_obj_t* ccard = lv_obj_create(parent);
  lv_obj_set_size(ccard, 308, 170);
  Theme::styleCard(ccard, outdoor);
  lv_obj_set_style_pad_all(ccard, 6, 0);
  lv_obj_remove_flag(ccard, LV_OBJ_FLAG_SCROLLABLE);

  _compose_ta = lv_textarea_create(ccard);
  lv_obj_set_size(_compose_ta, 202, 28);
  lv_obj_set_pos(_compose_ta, 0, 0);
  lv_textarea_set_one_line(_compose_ta, true);
  lv_textarea_set_placeholder_text(_compose_ta, "CLI: get wifi.ip, get name...");
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* run_btn = lv_button_create(ccard);
  lv_obj_set_size(run_btn, 88, 28);
  lv_obj_set_pos(run_btn, 206, 0);
  Theme::styleTouchButton(run_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(run_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    const char* cmd = (self->_compose_ta && lv_textarea_get_text(self->_compose_ta)[0])
                          ? lv_textarea_get_text(self->_compose_ta)
                          : "get wifi.ip";
    char rep[256] = {0};
    self->_actions.runCliCommand(cmd, rep, sizeof(rep));
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* runl = lv_label_create(run_btn);
  lv_obj_set_style_text_font(runl, &faro_font_12, 0);
  lv_label_set_text(runl, "Run CLI");
  lv_obj_center(runl);

  // Quick Region / Off-Grid / Diagnostics / Maintenance buttons: [Scope] [OffGrid] [DiagRF] [Wizard] [Reboot]
  static const char* kRegBtns[5] = {"Scope", "OffGrid", "DiagRF", "Wizard", "Reboot"};
  struct RegCtx { App* app; int idx; };
  static RegCtx s_rgctx[5];
  for (int i = 0; i < 5; ++i) {
    lv_obj_t* b = lv_button_create(ccard);
    lv_obj_set_size(b, 56, 28);
    lv_obj_set_pos(b, i * 59, 34);
    Theme::styleTouchButton(b, i == 4 ? p.danger : (i == 2 ? p.success : p.surface_alt),
                            (i == 4 || i == 2) ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_rgctx[i] = {this, i};
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      auto* ctx = static_cast<RegCtx*>(lv_event_get_user_data(e));
      App* a = ctx->app;
      if (ctx->idx == 0) {
        static const char* kScopes[4] = {"*", "#eu-mesh", "#ch-alps", "offgrid"};
        size_t next = 0;
        for (size_t s = 0; s < 4; ++s) {
          if (strcmp(a->_store.status().active_scope_name, kScopes[s]) == 0) {
            next = (s + 1) % 4;
            break;
          }
        }
        a->_actions.setDefaultScope(kScopes[next]);
      } else if (ctx->idx == 1) {
        RadioConfig cfg = a->_store.radio();
        cfg.freq_mhz = 869.618f;
        cfg.bw_khz = 62.5f;
        cfg.sf = 8;
        cfg.cr = 8;
        cfg.client_repeat = !cfg.client_repeat;
        a->_actions.setRadioConfig(cfg);
        a->_actions.setDefaultScope(cfg.client_repeat ? "offgrid(*)" : "*");
      } else if (ctx->idx == 2) {
        a->showScreen(ScreenId::Diagnostics);
        return;
      } else if (ctx->idx == 3) {
        a->showScreen(ScreenId::BootWizard);
        return;
      } else if (ctx->idx == 4) {
        a->_actions.rebootRadio();
      }
      a->renderBody();
    }, LV_EVENT_CLICKED, &s_rgctx[i]);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &faro_font_12, 0);
    lv_label_set_text(l, kRegBtns[i]);
    lv_obj_center(l);
  }

  lv_obj_t* clog = lv_label_create(ccard);
  lv_obj_set_pos(clog, 0, 68);
  lv_obj_set_width(clog, 294);
  lv_label_set_long_mode(clog, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(clog, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(clog, p.accent, 0);
  lv_label_set_text(clog, st.last_cli_reply);
}

void App::buildDiagnosticsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  const auto& st = _store.status();
  const auto& g = _store.gps();
  const auto& r = _store.radio();
  auto& up = _store.uiPrefs();

  // Top Sub-Tabs + Back Button for Diagnostics: [<] [Stats] [Sniff] [Graph] [RF] [Mesh]
  lv_obj_t* dbar = lv_obj_create(parent);
  lv_obj_set_size(dbar, 308, 28);
  lv_obj_set_style_bg_opa(dbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dbar, 0, 0);
  lv_obj_set_style_pad_all(dbar, 0, 0);
  lv_obj_set_flex_flow(dbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(dbar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back_btn = lv_button_create(dbar);
  lv_obj_set_size(back_btn, 32, 26);
  Theme::styleTouchButton(back_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
    static_cast<App*>(lv_event_get_user_data(e))->showScreen(ScreenId::Home);
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* bl = lv_label_create(back_btn);
  lv_obj_set_style_text_font(bl, &faro_font_12, 0);
  lv_label_set_text(bl, "<");
  lv_obj_center(bl);

  static const char* kDTabs[5] = {"Stats", "Sniff", "Graph", "RF", "Mesh"};
  struct DTabCtx { App* app; uint8_t t; };
  static DTabCtx s_dtctx[5];
  const uint8_t num_tabs = 5;
  auto tabWidth = [](uint8_t) { return 52; };
  for (uint8_t t = 0; t < num_tabs; ++t) {
    lv_obj_t* tb = lv_button_create(dbar);
    lv_obj_set_size(tb, tabWidth(t), 26);
    bool active = (_diag_tab == t);
    Theme::styleTouchButton(tb, active ? p.accent : p.surface_alt,
                            active ? lv_color_hex(0xFFFFFF) : p.text_primary);
    s_dtctx[t] = {this, t};
    lv_obj_add_event_cb(tb, [](lv_event_t* e) {
      auto* ctx = static_cast<DTabCtx*>(lv_event_get_user_data(e));
      ctx->app->_diag_tab = ctx->t;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_dtctx[t]);
    lv_obj_t* tl = lv_label_create(tb);
    lv_obj_set_style_text_font(tl, &faro_font_12, 0);
    lv_label_set_text(tl, kDTabs[t]);
    lv_obj_center(tl);
  }

  if (_diag_tab == 1) {
    // TAB 1: Live Packet Sniffer / Raw RX Analyzer (PUSH_CODE_LOG_RX_DATA = 0x88)
    lv_obj_t* sbar = lv_obj_create(parent);
    lv_obj_set_size(sbar, 308, 24);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sbar, 0, 0);
    lv_obj_set_style_pad_all(sbar, 0, 0);
    lv_obj_set_flex_flow(sbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cnt_lbl = lv_label_create(sbar);
    lv_obj_set_style_text_font(cnt_lbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(cnt_lbl, p.accent, 0);
    lv_label_set_text_fmt(cnt_lbl, "Live LoRa Packets (%zu)", _store.packetLogCount());

    lv_obj_t* ps_btn = lv_button_create(sbar);
    lv_obj_set_size(ps_btn, 58, 22);
    Theme::styleTouchButton(ps_btn, up.sniffer_paused ? p.warning : p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(ps_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().sniffer_paused = !self->_store.uiPrefs().sniffer_paused;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* psl = lv_label_create(ps_btn);
    lv_obj_set_style_text_font(psl, &faro_font_12, 0);
    lv_label_set_text(psl, up.sniffer_paused ? "Paused" : "Live");
    lv_obj_center(psl);

    lv_obj_t* cl_btn = lv_button_create(sbar);
    lv_obj_set_size(cl_btn, 50, 22);
    Theme::styleTouchButton(cl_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(cl_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.clearPacketLogs();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cll = lv_label_create(cl_btn);
    lv_obj_set_style_text_font(cll, &faro_font_12, 0);
    lv_label_set_text(cll, "Clear");
    lv_obj_center(cll);

    lv_obj_t* pbox = lv_obj_create(parent);
    lv_obj_set_size(pbox, 308, 110);
    Theme::styleCard(pbox, outdoor);
    lv_obj_set_style_pad_all(pbox, 3, 0);
    lv_obj_set_flex_flow(pbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pbox, 2, 0);

    if (_store.packetLogCount() == 0) {
      lv_obj_t* emp = lv_label_create(pbox);
      lv_obj_set_style_text_font(emp, &faro_font_mono_12, 0);
      lv_obj_set_style_text_color(emp, p.text_secondary, 0);
      lv_label_set_text(emp, "Listening for RF frames (869.618 MHz)...");
    } else {
      for (size_t i = _store.packetLogCount(); i > 0; --i) {
        const auto& pl = _store.packetLogAt(i - 1);
        if (!pl.valid) continue;
        lv_obj_t* row = lv_label_create(pbox);
        lv_obj_set_width(row, 294);
        lv_obj_set_style_text_font(row, &faro_font_mono_12, 0);
        lv_obj_set_style_text_color(row, p.text_primary, 0);
        lv_label_set_text_fmt(row, "[%-7.7s] %+ddB %ddBm %s",
                              pl.type_label, int(pl.snr_q4) / 4, int(pl.rssi), pl.summary);
      }
    }
    return;
  }

  if (_diag_tab == 2) {
    // TAB 2: Interactive 2D Mesh Topology Radar / Node-Link Graph
    constexpr int kW = 308;
    constexpr int kH = 138;
    constexpr int kCx = 154;
    constexpr int kCy = 70;
    const uint16_t bg_col   = outdoor ? 0xFFFFU : 0x0862U;
    const uint16_t ring_col = outdoor ? 0xD6BAU : 0x2145U;
    for (int i = 0; i < kW * kH; ++i) _map_canvas_buf[i] = bg_col;

    // Draw 1-hop repeater ring (r=32) & multi-hop mesh ring (r=58)
    drawCanvasCircle(_map_canvas_buf, kW, kH, kCx, kCy, 32, ring_col);
    drawCanvasCircle(_map_canvas_buf, kW, kH, kCx, kCy, 58, ring_col);

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, _map_canvas_buf, kW, kH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, kW, kH);

    // Count valid nodes & repeaters
    size_t valid_indices[10] = {0};
    size_t node_cnt = 0;
    size_t rpt_cnt = 0;
    for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
      const auto& c = _store.contacts()[i];
      if (!c.valid) continue;
      if (c.role == NodeRole::Repeater) ++rpt_cnt;
      if (node_cnt < 10) valid_indices[node_cnt++] = i;
    }

    int rpt_x = kCx, rpt_y = kCy;
    for (size_t k = 0; k < node_cnt; ++k) {
      const auto& c = _store.contacts()[valid_indices[k]];
      double angle = double(k) * (2.0 * 3.14159265358979323846 / double(node_cnt > 0 ? node_cnt : 1)) - 1.2;
      bool is_inner = (c.role == NodeRole::Repeater || (c.hasPath() && c.pathHops() <= 1));
      double rad = is_inner ? 32.0 : 58.0;
      int nx = kCx + int(rad * cos(angle));
      int ny = kCy + int(rad * sin(angle));
      if (c.role == NodeRole::Repeater && rpt_x == kCx) {
        rpt_x = nx;
        rpt_y = ny;
      }

      int snr_db = int(c.last_snr_q4) / 4;
      uint16_t link_col = (snr_db >= 4) ? 0x2668U : ((snr_db >= -2) ? 0xFD20U : 0xF800U);
      if (!is_inner && rpt_x != kCx) {
        drawCanvasLine(_map_canvas_buf, kW, kH, kCx, kCy, rpt_x, rpt_y, 0x2668U);
        drawCanvasLine(_map_canvas_buf, kW, kH, rpt_x, rpt_y, nx, ny, link_col);
      } else {
        drawCanvasLine(_map_canvas_buf, kW, kH, kCx, kCy, nx, ny, link_col);
      }

      lv_obj_t* nlbl = lv_label_create(canvas);
      lv_obj_set_style_text_font(nlbl, &faro_font_12, 0);
      lv_obj_set_style_text_color(nlbl, c.role == NodeRole::Repeater ? p.success : p.warning, 0);
      char snm[9] = {0};
      strncpy(snm, c.name, 7);
      lv_label_set_text_fmt(nlbl, "%s%+d", snm, snr_db);
      int lx = (nx < 16) ? 4 : ((nx > 236) ? 236 : (nx - 18));
      int ly = (ny < 16) ? 14 : ((ny > 120) ? 120 : (ny - 6));
      lv_obj_set_pos(nlbl, lx, ly);
    }

    lv_obj_t* me_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(me_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(me_lbl, p.accent, 0);
    lv_label_set_text(me_lbl, "[Me]");
    lv_obj_set_pos(me_lbl, kCx - 14, kCy - 7);

    lv_obj_t* top_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(top_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(top_lbl, p.text_primary, 0);
    lv_label_set_text_fmt(top_lbl, "Mesh Graph: %zu nodes (%zu RPT)", _store.contactCount(), rpt_cnt);
    lv_obj_set_pos(top_lbl, 4, 2);
    return;
  }

  if (_diag_tab == 3) {
    // TAB 3: SX1262 LoRa Link Budget & Time-on-Air Calculator + Range Test CSV Logger
    lv_obj_t* rcard = lv_obj_create(parent);
    lv_obj_set_size(rcard, 308, 138);
    Theme::styleCard(rcard, outdoor);
    lv_obj_set_style_pad_all(rcard, 4, 0);
    lv_obj_remove_flag(rcard, LV_OBJ_FLAG_SCROLLABLE);

    float toa_chat_ms = geomath::loraTimeOnAirMs(64, r.sf, r.bw_khz, r.cr);
    float toa_adv_ms  = geomath::loraTimeOnAirMs(104, r.sf, r.bw_khz, r.cr);
    float rx_sens_dbm = geomath::sx1262RxSensitivityDbm(r.sf, r.bw_khz);
    float link_bgt_db = geomath::loraLinkBudgetDb(r.tx_power_dbm, r.sf, r.bw_khz);
    uint32_t max_hr   = geomath::maxPacketsPerHour10Pct(toa_chat_ms);

    lv_obj_t* rl = lv_label_create(rcard);
    lv_obj_set_pos(rl, 0, 0);
    lv_obj_set_width(rl, 296);
    lv_obj_set_style_text_font(rl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(rl, p.text_primary, 0);
    lv_label_set_text_fmt(rl,
                          "SX1262: %.3fMHz BW:%.1fk SF%u CR4/%u %ddBm\n"
                          "RX Sens: %.1fdBm | Link Budget: %.1fdB\n"
                          "ToA 64B Chat: %.0fms | 104B Adv: %.0fms\n"
                          "10%% Duty: %u msg/h | RngTest: %zu pts",
                          double(r.freq_mhz), double(r.bw_khz), unsigned(r.sf),
                          unsigned(r.cr), int(r.tx_power_dbm),
                          double(rx_sens_dbm), double(link_bgt_db),
                          double(toa_chat_ms), double(toa_adv_ms),
                          unsigned(max_hr), _store.rangeSampleCount());

    // Range Test CSV controls: [RngTest:ON/OFF] [Log Point] [Clear CSV]
    lv_obj_t* rt_btn = lv_button_create(rcard);
    lv_obj_set_size(rt_btn, 102, 26);
    lv_obj_set_pos(rt_btn, 0, 88);
    Theme::styleTouchButton(rt_btn, up.range_test_active ? p.success : p.surface_alt, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(rt_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().range_test_active = !self->_store.uiPrefs().range_test_active;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* rtl = lv_label_create(rt_btn);
    lv_obj_set_style_text_font(rtl, &faro_font_12, 0);
    lv_label_set_text(rtl, up.range_test_active ? "RngTest:ON" : "RngTest:OFF");
    lv_obj_center(rtl);

    lv_obj_t* lp_btn = lv_button_create(rcard);
    lv_obj_set_size(lp_btn, 96, 26);
    lv_obj_set_pos(lp_btn, 106, 88);
    Theme::styleTouchButton(lp_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(lp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const auto& gp = self->_store.gps();
      const auto& stt = self->_store.status();
      if (gp.valid) {  // a range-test point without a fix would be fiction
        self->_store.recordRangeSample(gp.lat_deg, gp.lon_deg, stt.last_snr_q4, stt.last_rssi, 1,
                                       stt.uptime_secs);
      }
      self->_actions.sendZeroHopAdvert();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* lpl = lv_label_create(lp_btn);
    lv_obj_set_style_text_font(lpl, &faro_font_12, 0);
    lv_label_set_text(lpl, "Log Point");
    lv_obj_center(lpl);

    lv_obj_t* cr_btn = lv_button_create(rcard);
    lv_obj_set_size(cr_btn, 86, 26);
    lv_obj_set_pos(cr_btn, 206, 88);
    Theme::styleTouchButton(cr_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(cr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.clearRangeSamples();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* crl = lv_label_create(cr_btn);
    lv_obj_set_style_text_font(crl, &faro_font_12, 0);
    lv_label_set_text(crl, "Clear CSV");
    lv_obj_center(crl);
    return;
  }

  if (_diag_tab == 4) {
    // TAB 4 (Mesh):
    // PingBot/AwayBot, Store&Forward (S&F), Alpine Coords (WGS84/LV95/UTM), Custom Vars, JSON/GPX Export
    lv_obj_t* gcard = lv_obj_create(parent);
    lv_obj_set_size(gcard, 308, 138);
    Theme::styleCard(gcard, outdoor);
    lv_obj_set_style_pad_all(gcard, 4, 0);
    lv_obj_remove_flag(gcard, LV_OBJ_FLAG_SCROLLABLE);

    char pos_lines[96] = "Pos: no GPS fix\n";
    if (g.valid) {
      int32_t lv_e = 0, lv_n = 0;
      geomath::wgs84ToSwissLV95(g.lat_deg, g.lon_deg, lv_e, lv_n);
      int u_z = 0; char u_b = 'T'; int32_t u_e = 0, u_n = 0;
      geomath::wgs84ToUtm(g.lat_deg, g.lon_deg, u_z, u_b, u_e, u_n);
      snprintf(pos_lines, sizeof(pos_lines), "WGS:%.3f,%.3f | LV95:%d,%d\nUTM:%d%c %d %d",
               g.lat_deg, g.lon_deg, int(lv_e), int(lv_n), u_z, u_b, int(u_e), int(u_n));
    }

    static const char* kBotNames[3] = {"OFF", "PingBot", "AwayBot"};

    lv_obj_t* gl = lv_label_create(gcard);
    lv_obj_set_pos(gl, 0, 0);
    lv_obj_set_width(gl, 296);
    lv_obj_set_style_text_font(gl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(gl, p.text_primary, 0);
    lv_label_set_text_fmt(gl,
                          "Bot:%s(%u) | S&F:%s(%u)\n"
                          "%s | Vars:%.9s",
                          kBotNames[up.bot_mode % 3], unsigned(up.bot_replies_sent),
                          up.store_and_forward ? "ON" : "OFF", unsigned(up.sf_delivered_count),
                          pos_lines,
                          st.custom_vars_str);

    // Row 1: [Bot:OFF/Ping/Away] [S&F:ON/OFF] [Export JSON]
    lv_obj_t* bt_btn = lv_button_create(gcard);
    lv_obj_set_size(bt_btn, 94, 24);
    lv_obj_set_pos(bt_btn, 0, 62);
    Theme::styleTouchButton(bt_btn, up.bot_mode ? p.success : p.surface_alt, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(bt_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().bot_mode = (self->_store.uiPrefs().bot_mode + 1) % 3;
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* btl = lv_label_create(bt_btn);
    lv_obj_set_style_text_font(btl, &faro_font_12, 0);
    lv_label_set_text_fmt(btl, "Bot:%s", kBotNames[up.bot_mode % 3]);
    lv_obj_center(btl);

    lv_obj_t* sf_btn = lv_button_create(gcard);
    lv_obj_set_size(sf_btn, 98, 24);
    lv_obj_set_pos(sf_btn, 98, 62);
    Theme::styleTouchButton(sf_btn, up.store_and_forward ? p.warning : p.surface_alt, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(sf_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().store_and_forward = !self->_store.uiPrefs().store_and_forward;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* sfl = lv_label_create(sf_btn);
    lv_obj_set_style_text_font(sfl, &faro_font_12, 0);
    lv_label_set_text_fmt(sfl, "S&F:%s", up.store_and_forward ? "ON" : "OFF");
    lv_obj_center(sfl);

    lv_obj_t* jsn_btn = lv_button_create(gcard);
    lv_obj_set_size(jsn_btn, 94, 24);
    lv_obj_set_pos(jsn_btn, 200, 62);
    Theme::styleTouchButton(jsn_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(jsn_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char saved[96] = {0};
      self->_actions.exportContactsJson(saved, sizeof(saved));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* jsnl = lv_label_create(jsn_btn);
    lv_obj_set_style_text_font(jsnl, &faro_font_12, 0);
    lv_label_set_text(jsnl, "Export JSON");
    lv_obj_center(jsnl);

    // Row 2: [Get Vars] [Set gps:1] [Export GPX]
    lv_obj_t* rv_btn = lv_button_create(gcard);
    lv_obj_set_size(rv_btn, 94, 24);
    lv_obj_set_pos(rv_btn, 0, 92);
    Theme::styleTouchButton(rv_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(rv_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char vbuf[128] = {0};
      self->_actions.getCustomVars(vbuf, sizeof(vbuf));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* rvl = lv_label_create(rv_btn);
    lv_obj_set_style_text_font(rvl, &faro_font_12, 0);
    lv_label_set_text(rvl, "Get Vars");
    lv_obj_center(rvl);

    lv_obj_t* gv_btn = lv_button_create(gcard);
    lv_obj_set_size(gv_btn, 98, 24);
    lv_obj_set_pos(gv_btn, 98, 92);
    Theme::styleTouchButton(gv_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(gv_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_actions.setCustomVar("gps:1");
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* gvl = lv_label_create(gv_btn);
    lv_obj_set_style_text_font(gvl, &faro_font_12, 0);
    lv_label_set_text(gvl, "Set gps:1");
    lv_obj_center(gvl);

    lv_obj_t* gp_btn = lv_button_create(gcard);
    lv_obj_set_size(gp_btn, 94, 24);
    lv_obj_set_pos(gp_btn, 200, 92);
    Theme::styleTouchButton(gp_btn, p.surface_alt, p.text_primary);
    lv_obj_add_event_cb(gp_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char saved[96] = {0};
      self->_actions.exportGpsTrackGpx(saved, sizeof(saved));
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* gpl = lv_label_create(gp_btn);
    lv_obj_set_style_text_font(gpl, &faro_font_12, 0);
    lv_label_set_text(gpl, "Export GPX");
    lv_obj_center(gpl);
    return;
  }


  // TAB 0: System/RF Stats + Live 16-Sample RSSI & Noise Floor Sparkline Monitor
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, 308, 138);
  Theme::styleCard(card, outdoor);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* dl = lv_label_create(card);
  lv_obj_set_pos(dl, 0, 0);
  lv_obj_set_style_text_font(dl, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(dl, p.text_primary, 0);
  lv_label_set_text_fmt(dl,
                        "RSSI:%ddBm SNR:%+ddB Flr:%ddBm\n"
                        "TX:%u(D:%u F:%u) RX:%u Q:%u\n"
                        "Heap:%uKB PSRAM:%uKB Lp:%u/%ums\n"
                        "Duty:%.1f%%/10%%(%s) | Sparkline(16x):",
                        int(st.last_rssi), int(st.last_snr_q4) / 4, int(st.noise_floor_dbm),
                        unsigned(st.tx_packets), unsigned(st.direct_tx), unsigned(st.flood_tx),
                        unsigned(st.rx_packets), unsigned(st.queue_size),
                        unsigned(st.free_heap / 1024), unsigned(st.free_psram / 1024),
                        unsigned(st.last_loop_ms), unsigned(st.max_loop_ms),
                        double(st.duty_cycle_permille) / 10.0,
                        st.duty_cycle_throttled ? "THR" : "OK");

  // Render 16-bar RSSI & Noise Floor Sparkline at bottom of Stats card
  for (int i = 0; i < 16; ++i) {
    int16_t rssi_v = st.rssi_history[i];
    int bar_h = (int(rssi_v) + 122) / 2;
    if (bar_h < 4) bar_h = 4;
    if (bar_h > 28) bar_h = 28;
    lv_obj_t* bar = lv_obj_create(card);
    lv_obj_set_size(bar, 14, bar_h);
    lv_obj_set_pos(bar, i * 18 + 2, 116 - bar_h);
    lv_color_t bcol = (rssi_v >= -85) ? p.success : ((rssi_v >= -100) ? p.warning : p.danger);
    lv_obj_set_style_bg_color(bar, bcol, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  }
}

void App::buildBootWizardView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  createHeaderRow(parent, tr(StrId::BootWizardTitle, lang), outdoor, nullptr, nullptr);

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, 308, 134);
  Theme::styleCard(card, outdoor);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* info = lv_label_create(card);
  lv_obj_set_pos(info, 0, 0);
  lv_obj_set_width(info, 292);
  lv_obj_set_style_text_font(info, &faro_font_12, 0);
  lv_obj_set_style_text_color(info, p.text_primary, 0);
  const RadioRegion& reg = _store.regionInfo();
  lv_label_set_text_fmt(info,
                        "1. %s: %s (%s)\n"
                        "2. Node: %s\n"
                        "3. Region: %s\n"
                        "    %.3f MHz BW%.1f SF%u, max %d dBm",
                        tr(StrId::LanguageLabel, lang), languageName(lang), languageCode(lang),
                        _store.status().node_name, reg.name, double(_store.radio().freq_mhz),
                        double(_store.radio().bw_khz), unsigned(_store.radio().sf),
                        int(RegionPolicy::maxConductedDbm(reg)));

  // Region: applied at once (frequency, power ceiling, duty cycle of the region)
  lv_obj_t* reg_btn = lv_button_create(card);
  lv_obj_set_size(reg_btn, 140, 38);
  lv_obj_set_pos(reg_btn, 0, 80);
  Theme::styleTouchButton(reg_btn, p.warning, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(reg_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_actions.setRegion(RegionPolicy::next(self->_store.region()));
    self->updateStatusBar();
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* rl = lv_label_create(reg_btn);
  lv_obj_set_style_text_font(rl, &faro_font_14, 0);
  lv_label_set_text_fmt(rl, "%s >", reg.code);
  lv_obj_center(rl);

  lv_obj_t* done_btn = lv_button_create(card);
  lv_obj_set_size(done_btn, 146, 38);
  lv_obj_set_pos(done_btn, 146, 80);
  Theme::styleTouchButton(done_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(done_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    self->_store.uiPrefs().first_boot_done = true;
    self->showScreen(ScreenId::Home);
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* bl = lv_label_create(done_btn);
  lv_obj_set_style_text_font(bl, &faro_font_14, 0);
  lv_label_set_text(bl, tr(StrId::FinishWizard, lang));
  lv_obj_center(bl);
}

}  // namespace faro::ui
