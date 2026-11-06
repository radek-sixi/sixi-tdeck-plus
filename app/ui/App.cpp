// SPDX-License-Identifier: Apache-2.0
// app/ui/App.cpp — Touch-first 320x240 IPS LVGL 9 application shell & 15 screen views (ADR-018)
#include "App.h"
#include "Theme.h"
#include "fonts/FaroFonts.h"
#include "i18n/Strings.h"
#include "core/GeoMath.h"
#include "core/Random.h"

#include "AppUiShared.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#if defined(ARDUINO)
#include <esp_system.h>
#endif

#ifndef FARO_VERSION
#define FARO_VERSION "dev"
#endif
#ifndef FARO_PRODUCT_NAME
#define FARO_PRODUCT_NAME "Faro"
#endif

namespace faro::ui {

using namespace faro::core;
using namespace faro::ui::i18n;

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
    case ScreenId::Game:          return "16_snake";
    case ScreenId::Game2048:      return "17_2048";
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

void App::tick(uint32_t now_ms) {
  auto& up = _store.uiPrefs();

  // Battery, clock, WiFi/GPS state change without bumping the store version every second.
  if (now_ms - _last_status_ms >= 1000U) {
    _last_status_ms = now_ms;
    updateStatusBar();
  }

  // 1. PingBot / AwayBot Auto-Responder
  size_t mcnt = _store.messageCount();
  for (size_t i = 0; i < mcnt; ++i) {
    MessageRecord m = _store.messageAt(i);
    if (!m.valid || m.id <= _last_bot_msg_id) continue;
    _last_bot_msg_id = m.id;
    if (m.outgoing || up.bot_mode == 0) continue;
    bool is_own_message = strncmp(m.text, "[Faro Bot]", 10) == 0 ||
                          strncmp(m.text, "[AWAY]", 6) == 0;
    if (is_own_message) continue;

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


  if (_current_screen == ScreenId::Game) snakeTick(now_ms);

  if (_store.version() != _last_rendered_version) {
    updateStatusBar();
    renderBody();
    _last_rendered_version = _store.version();
  } else if (_current_screen == ScreenId::Map && _map_render_dirty &&
             now_ms - _map_last_render_ms >= 40U) {
    // Finish a pan that was coalesced while the touch event stream was running.
    renderMapCanvas();
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
  _thread_search_active = false;
  _thread_search_query[0] = '\0';
  if (id != ScreenId::ChatThread && id != ScreenId::ChannelThread) _thread_unread_id = 0;
  // Clear draft text on screen switch so sensitive inputs (BLE PIN, WiFi pwd, etc.) never leak across screens
  _draft_text[0] = '\0';
  _draft_key = 0xFFFFFFFFU;
  _compose_key = 0xFFFFFFFFU;
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
  _thread_unread_id = 0;
  if (contact_idx < AppStore::kMaxContacts && _store.contacts()[contact_idx].valid) {
    const ContactRecord& c = _store.contacts()[contact_idx];
    if (c.unread_count > 0) {
      size_t matching = 0;
      for (size_t i = 0; i < _store.messageCount(); ++i) {
        const auto& m = _store.messageAt(i);
        if (m.valid && !m.is_channel && memcmp(m.peer_prefix, c.pub_key, 6) == 0) ++matching;
      }
      const size_t skip = matching > c.unread_count ? matching - c.unread_count : 0;
      size_t seen = 0;
      for (size_t i = 0; i < _store.messageCount(); ++i) {
        const auto& m = _store.messageAt(i);
        if (!m.valid || m.is_channel || memcmp(m.peer_prefix, c.pub_key, 6) != 0) continue;
        if (seen++ == skip) { _thread_unread_id = m.id; break; }
      }
    }
    _store.clearContactUnread(c.pub_key);
    // Battery / temperature of the peer when a direct path is known; never floods the mesh just
    // because a chat was opened
    if (c.hasPath()) _actions.requestTelemetry(c.pub_key);
  }
  showScreen(ScreenId::ChatThread);
}

void App::openChannelThread(uint8_t channel_index) {
  _selected_channel_idx = channel_index;
  _thread_unread_id = 0;
  const ChannelRecord* ch = _store.findChannel(channel_index);
  if (ch && ch->unread_count > 0) {
    size_t matching = 0;
    for (size_t i = 0; i < _store.messageCount(); ++i) {
      const auto& m = _store.messageAt(i);
      if (m.valid && m.is_channel && m.channel_index == channel_index) ++matching;
    }
    const size_t skip = matching > ch->unread_count ? matching - ch->unread_count : 0;
    size_t seen = 0;
    for (size_t i = 0; i < _store.messageCount(); ++i) {
      const auto& m = _store.messageAt(i);
      if (!m.valid || !m.is_channel || m.channel_index != channel_index) continue;
      if (seen++ == skip) { _thread_unread_id = m.id; break; }
    }
  }
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
          if (_thread_search_active) {
            strncpy(_thread_search_query, txt, sizeof(_thread_search_query) - 1);
            _thread_search_query[sizeof(_thread_search_query) - 1] = '\0';
            renderBody();
          } else {
            const auto& c = _store.contacts()[_selected_contact_idx];
            uint32_t crc = 0;
            DeliveryState st = DeliveryState::Queued;
            _actions.sendDirectMessage(c.pub_key, txt, crc, st);
            renderBody();
          }
        } else if (_current_screen == ScreenId::Channels && _channels_subpage) {
          _actions.addHashtagChannel(txt);
          _channels_subpage = false;
          renderBody();
        } else if (_current_screen == ScreenId::ChannelThread) {
          if (_channel_thread_subpage) {
            _actions.addHashtagChannel(txt);
            _channel_thread_subpage = false;
          } else if (_thread_search_active) {
            strncpy(_thread_search_query, txt, sizeof(_thread_search_query) - 1);
            _thread_search_query[sizeof(_thread_search_query) - 1] = '\0';
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

  // 2048 controls when the 2048 screen is open.
  if (_current_screen == ScreenId::Game2048) {
    const uint8_t k = (key >= 'A' && key <= 'Z') ? uint8_t(key + 32) : key;
    if (k == 'w') { move2048(0); return; }
    if (k == 's') { move2048(1); return; }
    if (k == 'a') { move2048(2); return; }
    if (k == 'd') { move2048(3); return; }
    if (k == 'n') { new2048(); return; }
    if (k == 'q') { showScreen(ScreenId::Home); return; }
    return;
  }

  // Snake controls when the Easter egg screen is open.
  if (_current_screen == ScreenId::Game) {
    const uint8_t k = (key >= 'A' && key <= 'Z') ? uint8_t(key + 32) : key;
    if (k == 'w') { snakeSetDirection(SnakeDir::Up); return; }
    if (k == 's') { snakeSetDirection(SnakeDir::Down); return; }
    if (k == 'a') { snakeSetDirection(SnakeDir::Left); return; }
    if (k == 'd') { snakeSetDirection(SnakeDir::Right); return; }
    if (k == ' ') {
      if (!_snake_game_over) {
        _snake_paused = !_snake_paused;
        _snake_last_step_ms = 0;
        updateSnakeHud();
      }
      return;
    }
    if (k == 'n') { snakeReset(); return; }
    if (k == 'q') { showScreen(ScreenId::Home); return; }
    return;
  }

  // Easter eggs: type "snake", "tile" or "2048" while no text field is focused. "tile" exists
  // because the T-Deck keyboard layer for digits is awkward; letters work everywhere.
  {
    static char secret[5] = {0};
    static uint8_t secret_len = 0;
    const uint8_t k = (key >= 'A' && key <= 'Z') ? uint8_t(key + 32) : key;
    const bool is_code_char = (k >= 'a' && k <= 'z') || (k >= '0' && k <= '9');
    if (is_code_char) {
      if (secret_len >= sizeof(secret)) {
        memmove(secret, secret + 1, sizeof(secret) - 1);
        secret[sizeof(secret) - 1] = char(k);
      } else {
        secret[secret_len++] = char(k);
      }
      if (secret_len == 4 && (memcmp(secret, "2048", 4) == 0 || memcmp(secret, "tile", 4) == 0)) {
        secret_len = 0;
        new2048();
        showScreen(ScreenId::Game2048);
        return;
      }
      if (secret_len == 5 && memcmp(secret, "snake", 5) == 0) {
        secret_len = 0;
        _snake_len = 0;  // always start a fresh run from the Easter egg
        snakeReset();
        showScreen(ScreenId::Game);
        return;
      }
    } else {
      secret_len = 0;
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
  lv_obj_set_size(_top_left_lbl, 148, 16);
  lv_label_set_long_mode(_top_left_lbl, LV_LABEL_LONG_DOT);
  lv_obj_align(_top_left_lbl, LV_ALIGN_LEFT_MID, 0, 0);

  _top_right_lbl = lv_label_create(_top_bar);
  lv_obj_set_style_text_font(_top_right_lbl, &faro_font_12, 0);
  lv_obj_set_style_text_color(_top_right_lbl, p.text_primary, 0);
  lv_obj_set_size(_top_right_lbl, 160, 16);
  lv_label_set_long_mode(_top_right_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(_top_right_lbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(_top_right_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

  bool hide_bottom_nav = (_current_screen == ScreenId::ChatThread ||
                          _current_screen == ScreenId::ChannelThread ||
                          _current_screen == ScreenId::BootWizard ||
                          _current_screen == ScreenId::Game ||
                          _current_screen == ScreenId::Game2048 ||
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
  const char* dm_tag = "";
  snprintf(left_buf, sizeof(left_buf), "%.7s%s%s|%.6s|%s%.4s",
           st.node_name,
           up.bot_mode == 1 ? "[P]" : (up.bot_mode == 2 ? "[A]" : ""),
           dm_tag,
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
  uint32_t sub = (uint32_t(_settings_tab & 0x0F) << 8) |
                 (uint32_t(_wifi_edit_pwd ? 1 : 0) << 7) |
                 (uint32_t(_settings_hub ? 1 : 0) << 6) |
                 (uint32_t(_contact_detail_tab & 0x07) << 3) |
                 (uint32_t(_home_subpage || _chats_subpage || _channels_subpage || _channel_thread_subpage) << 2) |
                 (uint32_t(_screen_locked ? 1 : 0));
  return (uint32_t(_current_screen) << 24) ^
         (uint32_t(_selected_contact_idx & 0xFF) << 16) ^
         (uint32_t(_selected_channel_idx & 0x0F) << 12) ^
         sub;
}

void App::renderBody() {
  if (!_body) return;
  const uint32_t next_key = draftKey();
  if (_compose_ta && _compose_key == next_key) {
    // Only preserve draft text when re-rendering the exact same view/tab, and never for password/PIN fields
    bool is_pwd = lv_textarea_get_password_mode(_compose_ta) ||
                  (_current_screen == ScreenId::Settings && _settings_tab == 5);
    if (!is_pwd) {
      const char* cur = lv_textarea_get_text(_compose_ta);
      strncpy(_draft_text, cur ? cur : "", sizeof(_draft_text) - 1);
      _draft_text[sizeof(_draft_text) - 1] = '\0';
      _draft_key = _compose_key;
      _draft_focused = _input_group && lv_group_get_focused(_input_group) == _compose_ta;
    } else {
      _draft_text[0] = '\0';
      _draft_key = 0xFFFFFFFFU;
    }
  } else {
    _draft_text[0] = '\0';
    _draft_key = 0xFFFFFFFFU;
  }
  lv_obj_clean(_body);
  lv_obj_set_style_pad_row(_body, 5, 0);
  _compose_ta = nullptr;
  _map_canvas_obj = nullptr;
  _map_render_dirty = false;
  _snake_canvas = nullptr;
  _snake_score_lbl = nullptr;
  _snake_status_lbl = nullptr;
  _snake_pause_lbl = nullptr;
  _2048_score_lbl = nullptr;
  _2048_status_lbl = nullptr;
  for (size_t i = 0; i < 16; ++i) _2048_tiles[i] = nullptr;

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
    case ScreenId::Game:          buildGameView(_body); break;
    case ScreenId::Game2048:      buildGame2048View(_body); break;
    default:                      buildHomeView(_body); break;
  }

  if (_compose_ta) {
    if (_draft_key == next_key && _draft_text[0]) {
      lv_textarea_set_text(_compose_ta, _draft_text);
    }
    _compose_key = next_key;
    const auto& p = Theme::palette(_store.uiPrefs().outdoor_high_contrast);
    lv_obj_set_style_bg_color(_compose_ta, p.surface, 0);
    lv_obj_set_style_bg_opa(_compose_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(_compose_ta, p.text_primary, 0);
    lv_obj_set_style_text_color(_compose_ta, p.text_secondary, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(_compose_ta, p.border, 0);
    lv_obj_set_style_border_width(_compose_ta, 1, 0);
    lv_obj_set_style_radius(_compose_ta, 8, 0);

    // Clear visual focus indicator on the textbox (2px accent border + focus ring)
    lv_obj_set_style_border_color(_compose_ta, p.accent, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(_compose_ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(_compose_ta, p.accent, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(_compose_ta, 2, LV_STATE_FOCUS_KEY);
    Theme::styleFocusRing(_compose_ta);

    // High-contrast blinking cursor that contrasts in both Light and Dark themes
    const lv_style_selector_t cur_sel =
        lv_style_selector_t(LV_PART_CURSOR) | lv_style_selector_t(LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(_compose_ta, p.accent, cur_sel);
    lv_obj_set_style_bg_opa(_compose_ta, LV_OPA_30, cur_sel);
    lv_obj_set_style_text_color(_compose_ta, p.text_primary, cur_sel);
    lv_obj_set_style_border_color(_compose_ta, p.accent, cur_sel);
    lv_obj_set_style_border_width(_compose_ta, 2, cur_sel);
    lv_obj_set_style_border_side(_compose_ta, LV_BORDER_SIDE_LEFT, cur_sel);

    // Focus the textbox on touch click and ensure it is focused when present
    if (_input_group) {
      lv_group_focus_obj(_compose_ta);
    }
    lv_obj_add_event_cb(_compose_ta, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (self && self->_input_group && ta) {
        lv_group_focus_obj(ta);
      }
    }, LV_EVENT_CLICKED, this);
  }

  if (_show_wifi_ap_modal && _current_screen == ScreenId::Settings && _settings_tab == 4 &&
      !_settings_hub && _store.status().wifi_ap_active) {
    buildWifiApModal();
  }
}

}  // namespace faro::ui
