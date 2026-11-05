// SPDX-License-Identifier: Apache-2.0
// app/ui/App.h — Touch-first 320x240 IPS LVGL 9 application shell & 15 screen views (ADR-018)
#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include "core/AppStore.h"
#include "core/MeshActions.h"
#include "core/OfflineTileReader.h"

namespace faro::ui {

enum class ScreenId : uint8_t {
  Home = 0,
  Chats,
  ChatThread,
  Channels,
  ChannelThread,
  Discover,
  Contacts,
  ContactDetail,
  TracePath,
  Map,
  Settings,
  Diagnostics,
  BootWizard,
  Game,
  _Count
};

class App {
public:
  App(core::AppStore& store, core::IMeshActions& actions, core::OfflineTileReader& tile_reader);

  void begin();
  void tick(uint32_t now_ms);

  void showScreen(ScreenId id);
  ScreenId currentScreen() const { return _current_screen; }
  void setDiagTab(uint8_t tab) { _diag_tab = tab; }
  // Simulator / tests: open a settings page or a contact tab directly
  void setSettingsTab(uint8_t tab) {
    _settings_tab = tab;
    _settings_hub = false;
  }
  void setContactDetailTab(uint8_t tab) { _contact_detail_tab = tab; }

  void openChatWithContact(size_t contact_idx);
  void openChannelThread(uint8_t channel_index);
  void openContactDetail(size_t contact_idx);

  // Keyboard & shortcut handler (for BBQ10 physical keyboard and simulator hotkeys)
  void onKeyPress(uint8_t key);
  // True while a text field is on screen (keys go into it)
  bool isTyping() const { return _compose_ta != nullptr; }
  void openSettings() {
    _settings_hub = true;
    showScreen(ScreenId::Settings);
  }

  lv_group_t* inputGroup() const { return _input_group; }

  static const char* screenSlug(ScreenId id);

  // Lists show at most this many rows (newest first); filters and search reach the rest
  static constexpr size_t kMaxListRows = 40;

private:
  // Indexes of valid contacts that pass `keep`, favourites first, then by last advert (newest first)
  template <typename Pred>
  size_t sortedContacts(size_t* out, size_t max_out, Pred keep) const;
  void rebuildChrome();
  void updateStatusBar();
  void renderBody();

  void buildHomeView(lv_obj_t* parent);
  void buildChatsView(lv_obj_t* parent);
  void buildChatThreadView(lv_obj_t* parent);
  void buildChannelsView(lv_obj_t* parent);
  void buildChannelThreadView(lv_obj_t* parent);
  void buildDiscoverView(lv_obj_t* parent);
  void buildContactsView(lv_obj_t* parent);
  void buildContactDetailView(lv_obj_t* parent);
  void buildTracePathView(lv_obj_t* parent);
  void buildMapView(lv_obj_t* parent);
  void computeMapBase(double& out_lat, double& out_lon);
  void renderMapCanvas();
  void buildSettingsView(lv_obj_t* parent);
  void buildDiagnosticsView(lv_obj_t* parent);
  void buildBootWizardView(lv_obj_t* parent);
  void buildGameView(lv_obj_t* parent);

  // Easter egg: a small Snake game (touch swipe, WASD, trackball/keyboard pause).
  enum class SnakeDir : uint8_t { Up, Down, Left, Right };
  static constexpr int kSnakeCols = 20;
  static constexpr int kSnakeRows = 12;
  static constexpr int kSnakeCellPx = 11;
  static constexpr int kSnakeMax = kSnakeCols * kSnakeRows;
  void snakeReset();
  void snakeStep();
  void snakeSetDirection(SnakeDir dir);
  void snakeTick(uint32_t now_ms);
  void renderSnakeCanvas();
  void updateSnakeHud();
  void snakeSpawnFood();

  core::AppStore& _store;
  core::IMeshActions& _actions;
  core::OfflineTileReader& _tiles;

  ScreenId _current_screen = ScreenId::Home;
  size_t   _selected_contact_idx = 0;
  uint8_t  _selected_channel_idx = 0;
  size_t   _preset_idx = 0;
  int      _map_zoom = 12;
  bool     _map_sd_synced = false;
  double   _map_pan_lat_offset = 0.0;
  double   _map_pan_lon_offset = 0.0;
  int32_t  _map_drag_last_x = 0;
  int32_t  _map_drag_last_y = 0;
  bool     _map_drag_moved = false;
  bool     _map_render_dirty = false;
  uint32_t _map_last_render_ms = 0;
  lv_obj_t* _map_canvas_obj = nullptr;
  bool     _show_tracks_overlay = true;
  uint8_t  _contact_filter = 0;       // 0=All, 1=Chat, 2=RPT, 3=Room, 4=Sens, 5=Fav, 6=Tag
  uint8_t  _discover_filter = 0;      // 0=All, 1=RPT, 2=Room, 3=Chat, 4=Sens
  bool     _discover_show_hash = false;
  uint8_t  _contact_detail_tab = 0;   // 0=Info/QR, 1=Path, 2=Tags/Notes, 3=Admin/CLI
  uint8_t  _settings_tab = 0;         // 0=Lang/Display, 1=Profile, 2=Radio, 3=Mesh, 4=WiFi, 5=BT, 6=GPS, 7=CLI/Regions, 8=SD Backup
  bool     _settings_hub = true;      // True = show 3x3 touch category grid in Settings
  bool     _mesh_tuning_page = false; // False = Contacts/Telem, True = RF Tuning/ACKs
  bool     _wifi_edit_pwd = false;    // False = edit WiFi SSID, True = edit WiFi Password
  bool     _wifi_show_pwd = false;    // Reveal the password currently typed in the field (never the stored one)
  static constexpr size_t kMaxScannedWifi = 8;
  char     _wifi_scan_ssids[kMaxScannedWifi][33] = {};
  int16_t  _wifi_scan_rssis[kMaxScannedWifi] = {};
  size_t   _wifi_scan_count = 0;
  bool     _screen_locked = false;    // Pocket Lock Screen with live RX preview & Quick Reply
  bool     _home_subpage = false;     // Dedicated Quick Actions subpage (Lock, Theme, My QR, Lang)
  bool     _chats_subpage = false;    // Dedicated Chats Tools subpage (Search, +URI, Contacts)
  bool     _chat_thread_subpage = false; // Dedicated ChatThread Options subpage (Flood, TS, Info)
  bool     _thread_search_active = false;
  char     _thread_search_query[32] = {0};
  uint32_t _thread_unread_id = 0;
  bool     _channels_subpage = false; // Dedicated Add Channel subpage (+#Tag, +AES)
  bool     _channel_thread_subpage = false; // Dedicated ChannelThread Options subpage (@Ment, Scope)
  bool     _discover_subpage = false; // Dedicated Discover Advert & Scan subpage
  bool     _contacts_subpage = false; // Dedicated Contacts Tools subpage (Export JSON, Search, +URI)
  uint8_t  _diag_tab = 0;             // 0=Stats, 1=Sniff, 2=Graph, 3=RFCalc, 4=Bot/Exports
  bool     _show_compose_helper = false;
  bool     _show_my_qr_modal = false;
  bool     _show_calc_modal = false;
  int32_t  _calc_acc = 0;
  int32_t  _calc_cur = 0;
  char     _calc_op = 0;
  uint32_t _last_rendered_version = 0;
  uint32_t _last_status_ms = 0;
  uint32_t _last_bot_msg_id = 0;

  lv_group_t* _input_group = nullptr;
  lv_obj_t* _root = nullptr;
  lv_obj_t* _top_bar = nullptr;
  lv_obj_t* _top_left_lbl = nullptr;
  lv_obj_t* _top_right_lbl = nullptr;
  lv_obj_t* _body = nullptr;
  lv_obj_t* _bottom_bar = nullptr;
  lv_obj_t* _compose_ta = nullptr;
  // Draft kept across renderBody() so incoming traffic never wipes what the user is typing
  char      _draft_text[160] = {0};
  uint32_t  _draft_key = 0xFFFFFFFFU;
  uint32_t  _compose_key = 0xFFFFFFFFU;   // view that created _compose_ta
  bool      _draft_focused = false;
  uint32_t  draftKey() const;
  // Two-tap confirmation for dangerous actions (key export/import): returns true on the 2nd tap
  int       _armed_action = -1;
  uint32_t  _armed_ms = 0;
  bool      confirmTap(int action_id, const char* warning);
  // PingBot rate limiting
  uint32_t  _last_bot_reply_ms = 0;

  uint16_t _map_canvas_buf[308 * 144] = {0};
  size_t   _order[core::AppStore::kMaxContacts] = {};  // sorted list rows (one view at a time)

  // Snake easter egg. The 308x138 canvas reuses _map_canvas_buf, so it costs no extra framebuffer.
  uint8_t  _snake_cells[kSnakeMax] = {};
  uint8_t  _snake_len = 0;
  uint8_t  _snake_food = 0;
  SnakeDir _snake_dir = SnakeDir::Right;
  SnakeDir _snake_next_dir = SnakeDir::Right;
  uint32_t _snake_step_ms = 170;
  uint32_t _snake_last_step_ms = 0;
  uint16_t _snake_score = 0;
  uint16_t _snake_best = 0;
  bool     _snake_game_over = false;
  bool     _snake_paused = false;
  int16_t  _snake_drag_x = 0;
  int16_t  _snake_drag_y = 0;
  lv_obj_t* _snake_canvas = nullptr;
  lv_obj_t* _snake_score_lbl = nullptr;
  lv_obj_t* _snake_status_lbl = nullptr;
  lv_obj_t* _snake_pause_lbl = nullptr;
};

}  // namespace faro::ui
