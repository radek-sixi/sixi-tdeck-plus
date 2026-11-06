// SPDX-License-Identifier: Apache-2.0
// app/ui/AppViewsMessaging.cpp — Home, Chats, ChatThread, Channels, ChannelThread views (split out of App.cpp, ADR-018)
#include "App.h"
#include "AppUiShared.h"
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
      char emoji_txt[180] = {0};
      formatEmojiText(last_msg->text, emoji_txt, sizeof(emoji_txt));
      lv_label_set_text_fmt(mlbl, "%s (%s, %uh):\n%s",
                            last_msg->sender_name[0] ? last_msg->sender_name : "Peer",
                            last_msg->is_channel ? "Channel" : "DM",
                            unsigned(last_msg->hops),
                            emoji_txt);
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
  // Row 2: [3. Mesh graph]  [4. Diagnostics]
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
          char emoji_txt[180] = {0};
          formatEmojiText(last_msg->text, emoji_txt, sizeof(emoji_txt));
          lv_label_set_text_fmt(info, "[%u] %s | %s: %s",
                                unsigned(ch.index), ch.name, last_msg->sender_name, emoji_txt);
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
  static PeerCtx s_peer_ctx[kMaxListRows];  // one per shown row (static RAM stays small)
  size_t* s_order = _order;
  const UiPrefs& sup = _store.uiPrefs();
  const uint8_t cf = _contact_filter;
  const size_t n_match = sortedContacts(s_order, AppStore::kMaxContacts, [&](const ContactRecord& c) {
    if (cf == 1 && c.role != NodeRole::ChatNode) return false;
    if (cf == 2 && c.role != NodeRole::Repeater && c.role != NodeRole::RoomServer) return false;
    if (cf == 3 && c.role != NodeRole::RoomServer) return false;
    if (cf == 4 && c.role != NodeRole::Sensor) return false;
    if (cf == 5 && !c.favourite) return false;
    if (cf == 6 && c.tag_mask == 0) return false;
    if (sup.search_active && sup.search_query[0] != '\0' && strstr(c.name, sup.search_query) == nullptr &&
        strstr(c.notes, sup.search_query) == nullptr) {
      return false;
    }
    return true;
  });

  for (size_t k = 0; k < n_match && k < kMaxListRows; ++k) {
    const size_t i = s_order[k];
    const auto& c = _store.contacts()[i];

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 40);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_peer_ctx[k] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<PeerCtx*>(lv_event_get_user_data(e));
      if (!ctx || !ctx->app) return;
      ctx->app->openChatWithContact(ctx->idx);
    }, LV_EVENT_CLICKED, &s_peer_ctx[k]);

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
    }, LV_EVENT_CLICKED, &s_peer_ctx[k]);
    lv_obj_t* ol = lv_label_create(open_btn);
    lv_obj_set_style_text_font(ol, &faro_font_12, 0);
    if (c.role == NodeRole::Repeater || c.role == NodeRole::Sensor) {
      lv_label_set_text(ol, "Admin");
    } else {
      lv_label_set_text_fmt(ol, c.unread_count > 0 ? "Chat(%u)" : "Chat", unsigned(c.unread_count));
    }
    lv_obj_center(ol);
  }
  if (n_match > kMaxListRows) {
    lv_obj_t* more = lv_label_create(parent);
    lv_obj_set_style_text_font(more, &faro_font_12, 0);
    lv_obj_set_style_text_color(more, p.text_secondary, 0);
    lv_label_set_text_fmt(more, "+%u more: use a filter or search", unsigned(n_match - kMaxListRows));
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
    static CTOptCtx s_ctctx[6];
    const char* kCTLabels[6] = {
      "Contact Details & QR",
      "Reset Path to Flood",
      _store.uiPrefs().show_timestamps ? "Timestamps: ON" : "Timestamps: OFF",
      "Remote Admin / CLI",
      _thread_search_active ? "Exit search" : "Search text",
      "Clear chat"
    };
    static const uint32_t kCTCols[6] = {0x1A73E8, 0xE37400, 0x1E8E3E, 0x9334E6, 0x007B83, 0xD93025};

    for (int i = 0; i < 6; ++i) {
      lv_obj_t* b = lv_button_create(ocard);
      lv_obj_set_size(b, 294, 24);
      lv_obj_set_pos(b, 0, i * 26);
      Theme::styleTouchButton(b, lv_color_hex(kCTCols[i]), lv_color_hex(0xFFFFFF));
      s_ctctx[i] = {this, i};
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        auto* ctx = static_cast<CTOptCtx*>(lv_event_get_user_data(e));
        if (!ctx || !ctx->app) return;
        App* a = ctx->app;
        if (ctx->idx == 0) {
          a->_chat_thread_subpage = false;
          a->_contact_detail_tab = 0;
          a->openContactDetail(a->_selected_contact_idx);
        } else if (ctx->idx == 1) {
          a->_chat_thread_subpage = false;
          const auto& c = a->_store.contacts()[a->_selected_contact_idx];
          a->_actions.resetContactPath(c.pub_key);
          a->renderBody();
        } else if (ctx->idx == 2) {
          a->_chat_thread_subpage = false;
          a->_store.uiPrefs().show_timestamps = !a->_store.uiPrefs().show_timestamps;
          a->renderBody();
        } else if (ctx->idx == 3) {
          a->_chat_thread_subpage = false;
          a->_contact_detail_tab = 3;
          a->openContactDetail(a->_selected_contact_idx);
        } else if (ctx->idx == 4) {
          a->_chat_thread_subpage = false;
          a->_thread_search_active = !a->_thread_search_active;
          a->_thread_search_query[0] = '\0';
          a->renderBody();
        } else if (ctx->idx == 5) {
          auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
          if (!a->confirmTap(0x200 + int(a->_selected_contact_idx),
                             "Tap Clear chat again to delete this conversation")) {
            if (btn && lv_obj_get_child_count(btn) > 0) lv_label_set_text(lv_obj_get_child(btn, 0), "Tap again!");
            return;
          }
          const auto& c = a->_store.contacts()[a->_selected_contact_idx];
          a->_store.clearMessagesForPeer(c.pub_key);
          a->_chat_thread_subpage = false;
          a->renderBody();
        }
      }, LV_EVENT_CLICKED, &s_ctctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
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

  // Optional Canned / Emoticons / Diacritics / Clipboard Drawer (when _show_compose_helper == true)
  if (_show_compose_helper) {
    lv_obj_t* hbox = lv_obj_create(parent);
    lv_obj_set_size(hbox, 308, 124);
    Theme::styleCard(hbox, outdoor);
    lv_obj_set_style_pad_all(hbox, 3, 0);
    lv_obj_set_flex_flow(hbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hbox, 3, 0);

    // Quick Emoticon bar
    lv_obj_t* ebar = lv_obj_create(hbox);
    lv_obj_set_size(ebar, 294, 24);
    lv_obj_set_style_bg_opa(ebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ebar, 0, 0);
    lv_obj_set_style_pad_all(ebar, 0, 0);
    lv_obj_set_flex_flow(ebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ebar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ebar, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kEmoticons[6] = {
      " \xF0\x9F\x98\x80 ",  // 😀
      " \xF0\x9F\x98\x82 ",  // 😂
      " \xE2\x9D\xA4 ",      // ❤
      " \xF0\x9F\x91\x8D ",  // 👍
      " \xE2\x9C\x85 ",      // ✅
      " \xF0\x9F\x86\x98 "   // 🆘
    };
    static const char* kEmoLabels[6] = {
      "\xF0\x9F\x98\x80",
      "\xF0\x9F\x98\x82",
      "\xE2\x9D\xA4",
      "\xF0\x9F\x91\x8D",
      "\xE2\x9C\x85",
      "\xF0\x9F\x86\x98"
    };
    struct EmoCtx { App* app; const char* s; };
    static EmoCtx s_ectx[6];
    for (int e_i = 0; e_i < 6; ++e_i) {
      lv_obj_t* eb = lv_button_create(ebar);
      lv_obj_set_size(eb, 46, 22);
      Theme::styleTouchButton(eb, p.surface_alt, p.accent);
      s_ectx[e_i] = {this, kEmoticons[e_i]};
      lv_obj_add_event_cb(eb, [](lv_event_t* e) {
        auto* ctx = static_cast<EmoCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        if (a->_compose_ta) {
          lv_textarea_add_text(a->_compose_ta, ctx->s);
        }
        a->_show_compose_helper = false;
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_ectx[e_i]);
      lv_obj_t* elbl = lv_label_create(eb);
      lv_obj_set_style_text_font(elbl, &faro_font_12, 0);
      lv_label_set_text(elbl, kEmoLabels[e_i]);
      lv_obj_center(elbl);
    }

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
    lv_obj_t* target_bubble = nullptr;
    size_t shown_msgs = 0;
    for (size_t i = 0; i < _store.messageCount(); ++i) {
      const auto& m = _store.messageAt(i);
      if (!m.valid || m.is_channel) continue;
      if (memcmp(m.peer_prefix, peer->pub_key, 6) != 0) continue;
      if (_thread_search_active && _thread_search_query[0] &&
          !containsKeywordIgnoreCase(m.text, _thread_search_query)) {
        continue;
      }
      ++shown_msgs;

      char emoji_txt[180] = {0};
      formatEmojiText(m.text, emoji_txt, sizeof(emoji_txt));

      lv_obj_t* bubble = lv_label_create(msg_box);
      lv_obj_set_width(bubble, 290);
      lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(bubble, &faro_font_12, 0);
      lv_obj_set_style_text_color(bubble, m.outgoing ? p.accent : p.text_primary, 0);
      lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(bubble, reinterpret_cast<void*>(uintptr_t(m.id)));
      lv_obj_add_event_cb(bubble, [](lv_event_t* e) {
        auto* self = static_cast<App*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (!self || !obj) return;
        const uint32_t id = uint32_t(uintptr_t(lv_obj_get_user_data(obj)));
        if (self->_store.removeMessage(id)) self->renderBody();
      }, LV_EVENT_LONG_PRESSED, this);
      uint32_t hh = (m.timestamp / 3600U) % 24U;
      uint32_t mm = (m.timestamp / 60U) % 60U;
      if (m.outgoing) {
        unsigned heard_cnt = unsigned(m.repeats_heard + (m.delivery_state == DeliveryState::DeliveredAck ? 1 : 0));
        if (show_ts) {
          lv_label_set_text_fmt(bubble, "[%02u:%02u] Me [%s | Heard x%u]: %s",
                                unsigned(hh), unsigned(mm), deliveryBadge(m.delivery_state), heard_cnt, emoji_txt);
        } else {
          lv_label_set_text_fmt(bubble, "Me [%s | Heard x%u]: %s", deliveryBadge(m.delivery_state), heard_cnt, emoji_txt);
        }
      } else {
        if (show_ts) {
          lv_label_set_text_fmt(bubble, "[%02u:%02u] %s (%s%uh): %s",
                                unsigned(hh), unsigned(mm), m.sender_name,
                                m.hops == 0 ? "->" : "", unsigned(m.hops), emoji_txt);
        } else {
          lv_label_set_text_fmt(bubble, "%s (%uh): %s", m.sender_name, unsigned(m.hops), emoji_txt);
        }
      }
      if (!_thread_search_active && m.id == _thread_unread_id) target_bubble = bubble;
    }
    if (shown_msgs == 0 && _thread_search_active && _thread_search_query[0]) {
      lv_obj_t* empty_lbl = lv_label_create(msg_box);
      lv_obj_set_width(empty_lbl, 290);
      lv_obj_set_style_text_font(empty_lbl, &faro_font_12, 0);
      lv_obj_set_style_text_color(empty_lbl, p.text_secondary, 0);
      lv_label_set_text(empty_lbl, "No matching messages");
    }
    if (target_bubble) lv_obj_scroll_to_view(target_bubble, LV_ANIM_OFF);
    else lv_obj_scroll_to_y(msg_box, LV_COORD_MAX, LV_ANIM_OFF);
  }

  // Bottom compose bar: Textarea (150 char limit) + [:) / Can] + [Send] (34px tall)
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
  lv_textarea_set_placeholder_text(_compose_ta, _thread_search_active ? "Search this chat..." : "Message (max 150)...");
  if (_thread_search_active) lv_textarea_set_text(_compose_ta, _thread_search_query);
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* can_btn = lv_button_create(comp);
  lv_obj_set_size(can_btn, 62, 32);
  Theme::styleTouchButton(can_btn, _show_compose_helper ? p.accent : p.surface_alt,
                          _show_compose_helper ? lv_color_hex(0xFFFFFF) : p.text_primary);
  lv_obj_add_event_cb(can_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self->_thread_search_active) {
      self->_thread_search_active = false;
      self->_thread_search_query[0] = '\0';
    } else {
      self->_show_compose_helper = !self->_show_compose_helper;
    }
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* cl = lv_label_create(can_btn);
  lv_obj_set_style_text_font(cl, &faro_font_12, 0);
  lv_label_set_text(cl, _thread_search_active ? "Clear" : "\xF0\x9F\x98\x80 / Can");
  lv_obj_center(cl);

  lv_obj_t* send_btn = lv_button_create(comp);
  lv_obj_set_size(send_btn, 72, 32);
  Theme::styleTouchButton(send_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self || !self->_compose_ta) return;
    const char* cur = lv_textarea_get_text(self->_compose_ta);
    if (self->_thread_search_active) {
      strncpy(self->_thread_search_query, cur ? cur : "", sizeof(self->_thread_search_query) - 1);
      self->_thread_search_query[sizeof(self->_thread_search_query) - 1] = '\0';
      self->renderBody();
      return;
    }
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
  lv_label_set_text(sl, _thread_search_active ? "Find" : tr(StrId::BtnSend, lang));
  lv_obj_center(sl);
}

void App::buildChannelsView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  Language lang = _store.uiPrefs().language;
  const auto& p = Theme::palette(outdoor);

  if (_channels_subpage) {
    createHeaderRow(parent, "Channel Setup / #tag", outdoor,
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
    lv_textarea_set_placeholder_text(_compose_ta, "#hashtag or channel name...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* htag_btn = lv_button_create(ccard);
    lv_obj_set_size(htag_btn, 84, 28);
    lv_obj_set_pos(htag_btn, 210, 0);
    Theme::styleTouchButton(htag_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(htag_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      char nm[24] = {0};
      const char* txt = self->_compose_ta ? lv_textarea_get_text(self->_compose_ta) : nullptr;
      if (!txt || !txt[0]) return;  // nothing typed: no channel is made up
      snprintf(nm, sizeof(nm), "%s%s", txt[0] == '#' ? "" : "#", txt);
      self->_actions.addHashtagChannel(nm);
      self->_channels_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* htl = lv_label_create(htag_btn);
    lv_obj_set_style_text_font(htl, &faro_font_12, 0);
    lv_label_set_text(htl, "+ Join #Tag");
    lv_obj_center(htl);

    // Quick 1-tap hashtag channels of the selected region
    const char* const* kQuickTags = _store.regionInfo().quick_tags;
    struct QTagCtx { App* app; const char* tag; };
    static QTagCtx s_qtag_ctx[4];
    for (int q = 0; q < 4; ++q) {
      if (!kQuickTags[q] || !kQuickTags[q][0]) continue;
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
    if (ch && ch->name[0] == '#') lv_textarea_set_text(_compose_ta, ch->name);
    lv_textarea_set_placeholder_text(_compose_ta, "#hashtag...");
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    lv_obj_t* set_tag_btn = lv_button_create(ocard);
    lv_obj_set_size(set_tag_btn, 90, 28);
    lv_obj_set_pos(set_tag_btn, 204, 2);
    Theme::styleTouchButton(set_tag_btn, p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(set_tag_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      const char* txt = self->_compose_ta ? lv_textarea_get_text(self->_compose_ta) : nullptr;
      if (!txt || !txt[0]) return;
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
      const char* const* kScopes = nullptr;
      const size_t n_scopes = scopeNames(self->_store.region(), &kScopes);
      for (size_t i = 0; i < AppStore::kMaxChannels; ++i) {
        auto& c = self->_store.channelsMutable()[i];
        if (c.valid && c.index == self->_selected_channel_idx) {
          size_t next = 0;
          for (size_t s = 0; s < n_scopes; ++s) {
            if (strcmp(c.scope_name, kScopes[s]) == 0) {
              next = (s + 1) % n_scopes;
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

    lv_obj_t* find_btn = lv_button_create(ocard);
    lv_obj_set_size(find_btn, 144, 24);
    lv_obj_set_pos(find_btn, 0, 122);
    Theme::styleTouchButton(find_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(find_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self) return;
      self->_channel_thread_subpage = false;
      self->_thread_search_active = !self->_thread_search_active;
      self->_thread_search_query[0] = '\0';
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* findl = lv_label_create(find_btn);
    lv_obj_set_style_text_font(findl, &faro_font_12, 0);
    lv_label_set_text(findl, _thread_search_active ? "Exit search" : "Search text");
    lv_obj_center(findl);

    lv_obj_t* clr_btn = lv_button_create(ocard);
    lv_obj_set_size(clr_btn, 144, 24);
    lv_obj_set_pos(clr_btn, 150, 122);
    Theme::styleTouchButton(clr_btn, p.danger, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (!self || !btn) return;
      if (!self->confirmTap(0x300 + int(self->_selected_channel_idx),
                            "Tap Clear chat again to delete this channel history")) {
        if (lv_obj_get_child_count(btn) > 0) lv_label_set_text(lv_obj_get_child(btn, 0), "Tap again!");
        return;
      }
      self->_store.clearMessagesForChannel(self->_selected_channel_idx);
      self->_channel_thread_subpage = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* clrl = lv_label_create(clr_btn);
    lv_obj_set_style_text_font(clrl, &faro_font_12, 0);
    lv_label_set_text(clrl, "Clear chat");
    lv_obj_center(clrl);

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

  if (_show_compose_helper) {
    lv_obj_t* hbox = lv_obj_create(parent);
    lv_obj_set_size(hbox, 308, 124);
    Theme::styleCard(hbox, outdoor);
    lv_obj_set_style_pad_all(hbox, 3, 0);
    lv_obj_set_flex_flow(hbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hbox, 3, 0);

    lv_obj_t* ebar = lv_obj_create(hbox);
    lv_obj_set_size(ebar, 294, 24);
    lv_obj_set_style_bg_opa(ebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ebar, 0, 0);
    lv_obj_set_style_pad_all(ebar, 0, 0);
    lv_obj_set_flex_flow(ebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ebar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ebar, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kChEmoticons[6] = {
      " \xF0\x9F\x98\x80 ",  // 😀
      " \xF0\x9F\x98\x82 ",  // 😂
      " \xE2\x9D\xA4 ",      // ❤
      " \xF0\x9F\x91\x8D ",  // 👍
      " \xE2\x9C\x85 ",      // ✅
      " \xF0\x9F\x86\x98 "   // 🆘
    };
    static const char* kChEmoLabels[6] = {
      "\xF0\x9F\x98\x80",
      "\xF0\x9F\x98\x82",
      "\xE2\x9D\xA4",
      "\xF0\x9F\x91\x8D",
      "\xE2\x9C\x85",
      "\xF0\x9F\x86\x98"
    };
    struct ChEmoCtx { App* app; const char* s; };
    static ChEmoCtx s_ch_ectx[6];
    for (int e_i = 0; e_i < 6; ++e_i) {
      lv_obj_t* eb = lv_button_create(ebar);
      lv_obj_set_size(eb, 46, 22);
      Theme::styleTouchButton(eb, p.surface_alt, p.accent);
      s_ch_ectx[e_i] = {this, kChEmoticons[e_i]};
      lv_obj_add_event_cb(eb, [](lv_event_t* e) {
        auto* ctx = static_cast<ChEmoCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        if (a->_compose_ta) {
          lv_textarea_add_text(a->_compose_ta, ctx->s);
        }
        a->_show_compose_helper = false;
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_ch_ectx[e_i]);
      lv_obj_t* elbl = lv_label_create(eb);
      lv_obj_set_style_text_font(elbl, &faro_font_12, 0);
      lv_label_set_text(elbl, kChEmoLabels[e_i]);
      lv_obj_center(elbl);
    }

    struct ChCanCtx { App* app; size_t idx; };
    static ChCanCtx s_ch_can_ctx[AppStore::kMaxCanned];
    for (size_t ci = 0; ci < _store.cannedCount(); ++ci) {
      lv_obj_t* cb = lv_button_create(hbox);
      lv_obj_set_size(cb, 294, 22);
      Theme::styleTouchButton(cb, p.surface_alt, p.text_primary);
      s_ch_can_ctx[ci] = {this, ci};
      lv_obj_add_event_cb(cb, [](lv_event_t* e) {
        auto* ctx = static_cast<ChCanCtx*>(lv_event_get_user_data(e));
        App* a = ctx->app;
        a->_actions.sendChannelMessage(a->_selected_channel_idx, a->_store.cannedAt(ctx->idx));
        a->_show_compose_helper = false;
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_ch_can_ctx[ci]);
      lv_obj_t* clbl = lv_label_create(cb);
      lv_obj_set_style_text_font(clbl, &faro_font_12, 0);
      lv_label_set_text(clbl, _store.cannedAt(ci));
      lv_obj_center(clbl);
    }
  } else {
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

  lv_obj_t* target_bubble = nullptr;
  for (size_t i = 0; i < _store.messageCount(); ++i) {
    const auto& m = _store.messageAt(i);
    if (!m.valid || !m.is_channel || m.channel_index != _selected_channel_idx) continue;
    if (_thread_search_active && _thread_search_query[0] &&
        !containsKeywordIgnoreCase(m.text, _thread_search_query)) {
      continue;
    }

    bool is_mention = (my_name[0] != '\0' && strstr(m.text, my_name) != nullptr) ||
                      (strstr(m.text, "@[") != nullptr);
    if (!_thread_search_active && _store.uiPrefs().mention_only_filter && !is_mention) continue;

    ++shown_msgs;
    char emoji_txt[180] = {0};
    formatEmojiText(m.text, emoji_txt, sizeof(emoji_txt));

    lv_obj_t* lbl = lv_label_create(msg_box);
    lv_obj_set_width(lbl, 290);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(lbl, is_mention ? p.warning : (m.outgoing ? p.accent : p.text_primary), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(lbl, reinterpret_cast<void*>(uintptr_t(m.id)));
    lv_obj_add_event_cb(lbl, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (!self || !obj) return;
      const uint32_t id = uint32_t(uintptr_t(lv_obj_get_user_data(obj)));
      if (self->_store.removeMessage(id)) self->renderBody();
    }, LV_EVENT_LONG_PRESSED, this);
    if (m.outgoing) {
      lv_label_set_text_fmt(lbl, "%s [Heard x%u]: %s", m.sender_name, unsigned(m.repeats_heard + 1), emoji_txt);
    } else {
      lv_label_set_text_fmt(lbl, "%s%u> %s: %s", is_mention ? "@! " : "", unsigned(m.hops), m.sender_name, emoji_txt);
      if (rep_slot < 32) {
        s_rep_ctx[rep_slot] = {this, m.sender_name};
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
    if (!_thread_search_active && m.id == _thread_unread_id) target_bubble = lbl;
  }
  if (shown_msgs == 0) {
    lv_obj_t* empty_lbl = lv_label_create(msg_box);
    lv_obj_set_width(empty_lbl, 290);
    lv_label_set_long_mode(empty_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(empty_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(empty_lbl, p.text_secondary, 0);
    if (_thread_search_active && _thread_search_query[0]) {
      lv_label_set_text(empty_lbl, "No matching messages");
    } else {
      lv_label_set_text_fmt(empty_lbl, "[%s] Active on LoRa (%.3f MHz). No messages received yet — type below to broadcast.",
                            ch_name, double(_store.radio().freq_mhz));
    }
  }
  if (target_bubble) lv_obj_scroll_to_view(target_bubble, LV_ANIM_OFF);
  else lv_obj_scroll_to_y(msg_box, LV_COORD_MAX, LV_ANIM_OFF);
  }

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
  lv_textarea_set_placeholder_text(_compose_ta, _thread_search_active ? "Search this channel..." : "Broadcast to channel...");
  if (_thread_search_active) lv_textarea_set_text(_compose_ta, _thread_search_query);
  lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

  lv_obj_t* can_btn = lv_button_create(comp);
  lv_obj_set_size(can_btn, 62, 32);
  Theme::styleTouchButton(can_btn, _show_compose_helper ? p.accent : p.surface_alt,
                          _show_compose_helper ? lv_color_hex(0xFFFFFF) : p.text_primary);
  lv_obj_add_event_cb(can_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self->_thread_search_active) {
      self->_thread_search_active = false;
      self->_thread_search_query[0] = '\0';
    } else {
      self->_show_compose_helper = !self->_show_compose_helper;
    }
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* cl = lv_label_create(can_btn);
  lv_obj_set_style_text_font(cl, &faro_font_12, 0);
  lv_label_set_text(cl, _thread_search_active ? "Clear" : "\xF0\x9F\x98\x80 / Can");
  lv_obj_center(cl);

  lv_obj_t* send_btn = lv_button_create(comp);
  lv_obj_set_size(send_btn, 72, 32);
  Theme::styleTouchButton(send_btn, p.accent, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self || !self->_compose_ta) return;
    const char* cur = lv_textarea_get_text(self->_compose_ta);
    if (self->_thread_search_active) {
      strncpy(self->_thread_search_query, cur ? cur : "", sizeof(self->_thread_search_query) - 1);
      self->_thread_search_query[sizeof(self->_thread_search_query) - 1] = '\0';
      self->renderBody();
      return;
    }
    if (!cur || !cur[0]) return;  // empty box: nothing to send
    char txt[sizeof(self->_draft_text)] = {0};
    strncpy(txt, cur, sizeof(txt) - 1);
    lv_textarea_set_text(self->_compose_ta, "");
    self->_actions.sendChannelMessage(self->_selected_channel_idx, txt);
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* sl = lv_label_create(send_btn);
  lv_obj_set_style_text_font(sl, &faro_font_12, 0);
  lv_label_set_text(sl, _thread_search_active ? "Find" : tr(StrId::BtnSend, lang));
  lv_obj_center(sl);
}


}  // namespace faro::ui
