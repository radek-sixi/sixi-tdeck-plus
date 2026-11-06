// SPDX-License-Identifier: Apache-2.0
// app/ui/AppViewsSettings.cpp — the Settings view (split out of App.cpp, ADR-018)
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
  // [< Cat (62x28)] [Home (52x28)]   Category Title   [Next > (58x28)]
  lv_obj_t* sbar = lv_obj_create(parent);
  lv_obj_set_size(sbar, 308, 30);
  lv_obj_set_style_bg_opa(sbar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sbar, 0, 0);
  lv_obj_set_style_pad_all(sbar, 0, 0);
  lv_obj_set_flex_flow(sbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back_hub_btn = lv_button_create(sbar);
  lv_obj_set_size(back_hub_btn, 62, 28);
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
  lv_label_set_text(bhl, "< Cat");
  lv_obj_center(bhl);

  lv_obj_t* shome_btn = lv_button_create(sbar);
  lv_obj_set_size(shome_btn, 52, 28);
  Theme::styleTouchButton(shome_btn, p.surface_alt, p.accent);
  lv_obj_add_event_cb(shome_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ScreenId::Home);
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* shl = lv_label_create(shome_btn);
  lv_obj_set_style_text_font(shl, &faro_font_12, 0);
  lv_label_set_text(shl, "Home");
  lv_obj_center(shl);

  static const char* kSTabsEN[9] = {
    "7/9 Lang/Scr", "1/9 Node",     "2/9 Radio",    "3/9 Mesh",
    "4/9 WiFi",     "5/9 BLE",      "6/9 GPS",      "8/9 Reg/CLI",
    "9/9 SD Card"
  };
  lv_obj_t* cat_lbl = lv_label_create(sbar);
  lv_obj_set_style_text_font(cat_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(cat_lbl, p.text_primary, 0);
  lv_label_set_text(cat_lbl, kSTabsEN[_settings_tab % 9]);

  lv_obj_t* next_tab_btn = lv_button_create(sbar);
  lv_obj_set_size(next_tab_btn, 58, 28);
  Theme::styleTouchButton(next_tab_btn, p.surface_alt, p.accent);
  lv_obj_add_event_cb(next_tab_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    static const uint8_t kHubTabOrder[9] = {1, 2, 3, 4, 5, 6, 0, 7, 8};
    uint8_t next_tab = 1;
    for (int i = 0; i < 9; ++i) {
      if (kHubTabOrder[i] == (self->_settings_tab % 9)) {
        next_tab = kHubTabOrder[(i + 1) % 9];
        break;
      }
    }
    self->_settings_tab = next_tab;
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
      lv_obj_set_size(lb, 144, 30);
      lv_obj_set_pos(lb, (i % 2) * 150, 20 + (i / 2) * 36);
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
    lv_obj_set_size(th_btn, 94, 30);
    lv_obj_set_pos(th_btn, 0, 92);
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
    lv_obj_set_size(dim_btn, 94, 30);
    lv_obj_set_pos(dim_btn, 100, 92);
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
    lv_obj_set_size(lck_btn, 94, 30);
    lv_obj_set_pos(lck_btn, 200, 92);
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

    lv_obj_t* cpu_btn = lv_button_create(lcard);
    lv_obj_set_size(cpu_btn, 296, 28);
    lv_obj_set_pos(cpu_btn, 0, 126);
    Theme::styleTouchButton(cpu_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(cpu_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.uiPrefs().cpu_profile = uint8_t((self->_store.uiPrefs().cpu_profile + 1) % 4);
      self->_store.bumpVersion();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cpul = lv_label_create(cpu_btn);
    lv_obj_set_style_text_font(cpul, &faro_font_12, 0);
    static const char* kCpuLabels[4] = {
      "CPU: Auto (80 MHz when screen off)",
      "CPU: 240 MHz (performance)",
      "CPU: 160 MHz (balanced)",
      "CPU: 80 MHz (battery)"
    };
    lv_label_set_text(cpul, kCpuLabels[_store.uiPrefs().cpu_profile % 4]);
    lv_obj_center(cpul);
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

    // Row 1 (y=20): [Region] + [Preset / Frequency Drop-Down List]
    char rg_lbl[24];
    snprintf(rg_lbl, sizeof(rg_lbl), "%s", reg.code);
    bindRadBtn(lv_button_create(rcard), 12, 0, 20, 84, 32, p.warning, lv_color_hex(0xFFFFFF), rg_lbl);

    char dd_opts[512] = {0};
    size_t dd_pos = 0;
    for (uint8_t k = 0; k < reg.preset_count; ++k) {
      int n = snprintf(dd_opts + dd_pos, sizeof(dd_opts) - dd_pos, "%s%s (%.3f)",
                       k > 0 ? "\n" : "", reg.presets[k].name, double(reg.presets[k].freq_mhz));
      if (n > 0 && dd_pos + size_t(n) < sizeof(dd_opts)) dd_pos += size_t(n);
    }
    if (pi < 0) {
      snprintf(dd_opts + dd_pos, sizeof(dd_opts) - dd_pos, "%sCustom (%.3f)",
               dd_pos > 0 ? "\n" : "", double(r.freq_mhz));
    }
    (void)active_preset_name;

    lv_obj_t* pr_dd = lv_dropdown_create(rcard);
    lv_obj_set_size(pr_dd, 206, 32);
    lv_obj_set_pos(pr_dd, 88, 20);
    lv_dropdown_set_symbol(pr_dd, nullptr);
    lv_dropdown_set_options(pr_dd, dd_opts);
    lv_dropdown_set_selected(pr_dd, pi >= 0 ? uint32_t(pi) : uint32_t(reg.preset_count));
    lv_obj_set_style_text_font(pr_dd, &faro_font_12, 0);
    lv_obj_set_style_bg_color(pr_dd, p.accent, 0);
    lv_obj_set_style_bg_opa(pr_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(pr_dd, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(pr_dd, p.border, 0);
    lv_obj_set_style_border_width(pr_dd, 1, 0);
    lv_obj_set_style_pad_ver(pr_dd, 6, 0);
    lv_obj_add_event_cb(pr_dd, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (!self || !dd) return;
      lv_event_code_t code = lv_event_get_code(e);
      if (code == LV_EVENT_READY) {
        lv_obj_t* list = lv_dropdown_get_list(dd);
        if (list) {
          const auto& pal = Theme::palette(self->_store.uiPrefs().outdoor_high_contrast);
          lv_obj_set_style_text_font(list, &faro_font_12, 0);
          lv_obj_set_style_bg_color(list, pal.surface, 0);
          lv_obj_set_style_text_color(list, pal.text_primary, 0);
        }
      } else if (code == LV_EVENT_VALUE_CHANGED) {
        const RadioRegion& rg = self->_store.regionInfo();
        uint32_t sel = lv_dropdown_get_selected(dd);
        if (sel < rg.preset_count) {
          self->_preset_idx = sel;
          const auto& pr = rg.presets[sel];
          RadioConfig cfg = self->_store.radio();
          cfg.freq_mhz = pr.freq_mhz;
          cfg.bw_khz = pr.bw_khz;
          cfg.sf = pr.sf;
          cfg.cr = pr.cr;
          cfg.client_repeat = cfg.client_repeat && RegionPolicy::isRepeatFrequency(rg, pr.freq_mhz);
          if (self->_actions.setRadioConfig(cfg) && pr.path_hash_bytes != 0) {
            self->_actions.setPathHashMode(pr.path_hash_bytes);
          }
          self->updateStatusBar();
          self->renderBody();
        }
      }
    }, LV_EVENT_ALL, this);

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

  // SUBPAGE 4 (5/8): Dedicated WiFi Network Station Setup (Scan + SSID Dropdown + Password -> DHCP IP)
  if (_settings_tab == 4) {
    _wifi_show_pwd = false;  // a rebuild never re-reveals a password
    lv_obj_t* wcard = lv_obj_create(parent);
    lv_obj_set_size(wcard, 308, 170);
    Theme::styleCard(wcard, outdoor);
    lv_obj_set_style_pad_all(wcard, 6, 0);
    lv_obj_remove_flag(wcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wlbl = lv_label_create(wcard);
    lv_obj_set_pos(wlbl, 0, 0);
    lv_obj_set_width(wlbl, 230);
    lv_label_set_long_mode(wlbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(wlbl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(wlbl, st.wifi_connected ? p.success : p.text_primary, 0);
    lv_label_set_text_fmt(wlbl,
                          "WiFi:%s %s (%s)\n"
                          "A:%s %.10s %s",
                          st.wifi_enabled ? "ON" : "OFF",
                          st.wifi_connected ? "CONNECTED" : "DISCONN",
                          st.wifi_connected ? st.wifi_ip : "DHCP",
                          st.wifi_auth_enabled ? "PSK" : "OPEN",
                          st.wifi_ssid[0] ? st.wifi_ssid : "(not set)",
                          st.wifi_pwd[0] ? "Pwd:set" : "Pwd:none");


    // Scanned Wi-Fi Networks Dropdown + [Scan WiFi] button (y = 32)
    char w_opts[384] = {0};
    uint32_t w_sel = 0;
    if (_wifi_scan_count == 0) {
      snprintf(w_opts, sizeof(w_opts), "%s",
               st.wifi_ssid[0] ? st.wifi_ssid : "(Tap Scan to list WiFi...)");
    } else {
      size_t w_pos = 0;
      for (size_t k = 0; k < _wifi_scan_count; ++k) {
        if (st.wifi_ssid[0] && strcmp(_wifi_scan_ssids[k], st.wifi_ssid) == 0) {
          w_sel = uint32_t(k);
        }
        int n = snprintf(w_opts + w_pos, sizeof(w_opts) - w_pos, "%s%.18s (%ddBm)",
                         k > 0 ? "\n" : "", _wifi_scan_ssids[k], int(_wifi_scan_rssis[k]));
        if (n > 0 && w_pos + size_t(n) < sizeof(w_opts)) w_pos += size_t(n);
      }
    }

    lv_obj_t* w_dd = lv_dropdown_create(wcard);
    lv_obj_set_size(w_dd, 198, 22);
    lv_obj_set_pos(w_dd, 0, 30);
    lv_dropdown_set_symbol(w_dd, nullptr);
    lv_dropdown_set_options(w_dd, w_opts);
    lv_dropdown_set_selected(w_dd, w_sel);
    lv_obj_set_style_text_font(w_dd, &faro_font_12, 0);
    lv_obj_set_style_bg_color(w_dd, p.surface_alt, 0);
    lv_obj_set_style_bg_opa(w_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(w_dd, p.text_primary, 0);
    lv_obj_set_style_border_color(w_dd, p.border, 0);
    lv_obj_set_style_border_width(w_dd, 1, 0);
    lv_obj_set_style_pad_ver(w_dd, 3, 0);
    lv_obj_add_event_cb(w_dd, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (!self || !dd) return;
      lv_event_code_t code = lv_event_get_code(e);
      if (code == LV_EVENT_READY) {
        lv_obj_t* list = lv_dropdown_get_list(dd);
        if (list) {
          const auto& pal = Theme::palette(self->_store.uiPrefs().outdoor_high_contrast);
          lv_obj_set_style_text_font(list, &faro_font_12, 0);
          lv_obj_set_style_bg_color(list, pal.surface, 0);
          lv_obj_set_style_text_color(list, pal.text_primary, 0);
        }
      } else if (code == LV_EVENT_VALUE_CHANGED) {
        uint32_t sel = lv_dropdown_get_selected(dd);
        if (sel < self->_wifi_scan_count) {
          strncpy(self->_store.status().wifi_ssid, self->_wifi_scan_ssids[sel],
                  sizeof(self->_store.status().wifi_ssid) - 1);
          self->_store.status().wifi_ssid[sizeof(self->_store.status().wifi_ssid) - 1] = '\0';
          // Automatically switch textbox to Password entry after picking an SSID
          self->_wifi_edit_pwd = true;
          self->_wifi_show_pwd = false;
          self->renderBody();
        }
      }
    }, LV_EVENT_ALL, this);

    lv_obj_t* scan_btn = lv_button_create(wcard);
    lv_obj_set_size(scan_btn, 92, 22);
    lv_obj_set_pos(scan_btn, 202, 30);
    Theme::styleTouchButton(scan_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_t* scl = lv_label_create(scan_btn);
    lv_obj_set_style_text_font(scl, &faro_font_12, 0);
    lv_label_set_text_fmt(scl, _wifi_scan_count > 0 ? "Scan (%zu)" : "Scan WiFi", _wifi_scan_count);
    lv_obj_center(scl);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self) return;
      auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
      if (btn && lv_obj_get_child_count(btn) > 0) {
        lv_label_set_text(lv_obj_get_child(btn, 0), "Scanning...");
        lv_refr_now(nullptr);
      }
      self->_wifi_scan_count = self->_actions.scanWifiNetworks(
          self->_wifi_scan_ssids, self->_wifi_scan_rssis, 8);
      if (self->_wifi_scan_count > 0 && !self->_store.status().wifi_ssid[0]) {
        strncpy(self->_store.status().wifi_ssid, self->_wifi_scan_ssids[0],
                sizeof(self->_store.status().wifi_ssid) - 1);
        self->_wifi_edit_pwd = true;
        self->_wifi_show_pwd = false;
      }
      self->renderBody();
    }, LV_EVENT_CLICKED, this);

    _compose_ta = lv_textarea_create(wcard);
    lv_obj_set_size(_compose_ta, 198, 24);
    lv_obj_set_pos(_compose_ta, 0, 56);
    lv_textarea_set_one_line(_compose_ta, true);
    if (_wifi_edit_pwd) {
      // never put the stored password back on screen
      lv_textarea_set_password_mode(_compose_ta, true);
      lv_textarea_set_placeholder_text(_compose_ta, "WiFi password (hidden)...");
    } else {
      lv_textarea_set_text(_compose_ta, st.wifi_ssid);
      lv_textarea_set_placeholder_text(_compose_ta, "Type WiFi SSID (Name)...");
    }
    lv_obj_set_style_text_font(_compose_ta, &faro_font_12, 0);

    // Password visibility: toggling changes the textarea in place, so a half-typed password is
    // not lost by a render. The stored password is never put back into the field.
    if (_wifi_edit_pwd) {
      lv_obj_t* show_btn = lv_button_create(wcard);
      lv_obj_set_size(show_btn, 42, 24);
      lv_obj_set_pos(show_btn, 202, 56);
      Theme::styleTouchButton(show_btn, p.surface_alt, p.accent);
      lv_obj_add_event_cb(show_btn, [](lv_event_t* e) {
        auto* self = static_cast<App*>(lv_event_get_user_data(e));
        auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (!self || !btn) return;
        self->_wifi_show_pwd = !self->_wifi_show_pwd;
        if (self->_compose_ta) {
          lv_textarea_set_password_mode(self->_compose_ta, !self->_wifi_show_pwd);
        }
        if (lv_obj_get_child_count(btn) > 0) {
          lv_label_set_text(lv_obj_get_child(btn, 0), self->_wifi_show_pwd ? "Hide" : "Show");
        }
      }, LV_EVENT_CLICKED, this);
      lv_obj_t* shl = lv_label_create(show_btn);
      lv_obj_set_style_text_font(shl, &faro_font_12, 0);
      lv_label_set_text(shl, "Show");
      lv_obj_center(shl);
    }

    lv_obj_t* sv_btn = lv_button_create(wcard);
    lv_obj_set_size(sv_btn, _wifi_edit_pwd ? 48 : 92, 24);
    lv_obj_set_pos(sv_btn, _wifi_edit_pwd ? 248 : 202, 56);
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
      self->_wifi_show_pwd = false;
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* svl = lv_label_create(sv_btn);
    lv_obj_set_style_text_font(svl, &faro_font_12, 0);
    lv_label_set_text(svl, _wifi_edit_pwd ? "Save" : "Save SSID");
    lv_obj_center(svl);

    // 2x2 Grid of Touch Buttons (144x28 px)
    lv_obj_t* fld_btn = lv_button_create(wcard);
    lv_obj_set_size(fld_btn, 144, 24);
    lv_obj_set_pos(fld_btn, 0, 84);
    Theme::styleTouchButton(fld_btn, p.surface_alt, p.accent);
    lv_obj_add_event_cb(fld_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self->_compose_ta) {
        const char* cur = lv_textarea_get_text(self->_compose_ta);
        if (self->_wifi_edit_pwd) {
          if (cur && cur[0]) {
            strncpy(self->_store.status().wifi_pwd, cur, sizeof(self->_store.status().wifi_pwd) - 1);
          }
        } else {
          strncpy(self->_store.status().wifi_ssid, cur ? cur : "", sizeof(self->_store.status().wifi_ssid) - 1);
        }
      }
      self->_wifi_edit_pwd = !self->_wifi_edit_pwd;
      self->_wifi_show_pwd = false;
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* fldl = lv_label_create(fld_btn);
    lv_obj_set_style_text_font(fldl, &faro_font_12, 0);
    lv_label_set_text(fldl, _wifi_edit_pwd ? "Field: Password" : "Field: WiFi SSID");
    lv_obj_center(fldl);

    lv_obj_t* join_btn = lv_button_create(wcard);
    lv_obj_set_size(join_btn, 144, 24);
    lv_obj_set_pos(join_btn, 150, 84);
    Theme::styleTouchButton(join_btn, st.wifi_connected ? p.accent : p.success, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(join_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (self->_compose_ta) {
        const char* cur = lv_textarea_get_text(self->_compose_ta);
        if (self->_wifi_edit_pwd) {
          const char* pwd = (cur && cur[0]) ? cur : self->_store.status().wifi_pwd;
          self->_actions.setWifiConfig(true, self->_store.status().wifi_ssid, pwd);
        } else {
          self->_actions.setWifiConfig(true, cur ? cur : "", self->_store.status().wifi_pwd);
        }
      } else {
        self->_actions.setWifiConfig(true, self->_store.status().wifi_ssid, self->_store.status().wifi_pwd);
      }
      self->_wifi_show_pwd = false;
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* joinl = lv_label_create(join_btn);
    lv_obj_set_style_text_font(joinl, &faro_font_12, 0);
    lv_label_set_text(joinl, st.wifi_connected ? "Reconnect" : "Connect WiFi");
    lv_obj_center(joinl);

    lv_obj_t* pwr_btn = lv_button_create(wcard);
    lv_obj_set_size(pwr_btn, 144, 22);
    lv_obj_set_pos(pwr_btn, 0, 112);
    Theme::styleTouchButton(pwr_btn, st.wifi_enabled ? p.warning : p.surface_alt,
                            st.wifi_enabled ? lv_color_hex(0xFFFFFF) : p.text_primary);
    lv_obj_add_event_cb(pwr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      bool next = !self->_store.status().wifi_enabled;
      self->_actions.setWifiConfig(next, self->_store.status().wifi_ssid, self->_store.status().wifi_pwd);
      self->_wifi_show_pwd = false;
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* pwrl = lv_label_create(pwr_btn);
    lv_obj_set_style_text_font(pwrl, &faro_font_12, 0);
    lv_label_set_text_fmt(pwrl, "WiFi: %s", st.wifi_enabled ? "ON" : "OFF");
    lv_obj_center(pwrl);

    lv_obj_t* clr_btn = lv_button_create(wcard);
    lv_obj_set_size(clr_btn, 144, 22);
    lv_obj_set_pos(clr_btn, 150, 112);
    Theme::styleTouchButton(clr_btn, p.surface_alt, p.danger);
    lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      self->_store.status().wifi_ssid[0] = '\0';
      self->_store.status().wifi_pwd[0] = '\0';
      self->_actions.setWifiConfig(false, "", "");
      self->_wifi_show_pwd = false;
      self->updateStatusBar();
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* clrl = lv_label_create(clr_btn);
    lv_obj_set_style_text_font(clrl, &faro_font_12, 0);
    lv_label_set_text(clrl, "Forget Network");
    lv_obj_center(clrl);

    lv_obj_t* ap_btn = lv_button_create(wcard);
    lv_obj_set_size(ap_btn, 296, 22);
    lv_obj_set_pos(ap_btn, 0, 136);
    Theme::styleTouchButton(ap_btn, st.wifi_ap_active ? p.warning : p.surface_alt,
                            st.wifi_ap_active ? lv_color_hex(0xFFFFFF) : p.accent);
    lv_obj_add_event_cb(ap_btn, [](lv_event_t* e) {
      auto* self = static_cast<App*>(lv_event_get_user_data(e));
      if (!self) return;
      if (self->_store.status().wifi_ap_active) {
        self->_actions.stopWifiSetupAp();
        self->_show_wifi_ap_modal = false;
      } else {
        char ssid[33] = {0}, pwd[16] = {0}, ip[16] = {0};
        if (self->_actions.startWifiSetupAp(ssid, sizeof(ssid), pwd, sizeof(pwd), ip, sizeof(ip))) {
          self->_show_wifi_ap_modal = true;
        } else {
          strncpy(self->_store.status().last_cli_reply, "Setup AP failed to start",
                  sizeof(self->_store.status().last_cli_reply) - 1);
        }
      }
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* apl = lv_label_create(ap_btn);
    lv_obj_set_style_text_font(apl, &faro_font_12, 0);
    lv_label_set_text(apl, st.wifi_ap_active ? "Phone setup active - Stop" : "Setup WiFi via phone");
    lv_obj_center(apl);
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

    _compose_ta = lv_textarea_create(bcard);
    lv_obj_set_size(_compose_ta, 194, 28);
    lv_obj_set_pos(_compose_ta, 0, 48);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_max_length(_compose_ta, 6);
    lv_textarea_set_placeholder_text(_compose_ta, "New 6-digit BLE PIN...");
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
        lv_textarea_set_text(self->_compose_ta, "");
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
      self->_actions.setBlePin(0);  // 0 = reset BLE bonds & generate fresh random PIN shown on screen
      self->renderBody();
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* p2l = lv_label_create(p2_btn);
    lv_obj_set_style_text_font(p2l, &faro_font_12, 0);
    lv_label_set_text(p2l, "Reset BLE / PIN");
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
          s.tz_offset_hours = (s.tz_offset_hours >= 14) ? int8_t(-12) : int8_t(s.tz_offset_hours + 1);
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
    lv_obj_set_width(sdlbl, 294);
    lv_label_set_long_mode(sdlbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(sdlbl, &faro_font_mono_12, 0);
    bool is_err = strncmp(st.sd_backup_status, "ERR", 3) == 0;
    bool is_ok  = strncmp(st.sd_backup_status, "OK", 2) == 0;
    lv_obj_set_style_text_color(sdlbl, is_err ? p.danger : (is_ok ? p.success : p.text_primary), 0);
    lv_label_set_text_fmt(sdlbl,
                          "SD Card: %s (%u/%u MB) | Overwrite: ON\n"
                          "Files: /faro/settings.bin + /faro/identity.bak\n"
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
    const uint32_t now_tick = lv_tick_get();
    struct SdCtx { App* app; int idx; };
    static SdCtx s_sdctx[6];
    for (int i = 0; i < 6; ++i) {
      bool armed_here = (_armed_action == i && (now_tick - _armed_ms <= 5000U));
      lv_obj_t* b = lv_button_create(sdcard);
      lv_obj_set_size(b, 144, 32);
      lv_obj_set_pos(b, (i % 2) * 150, 50 + (i / 2) * 36);
      Theme::styleTouchButton(b, armed_here ? p.danger : lv_color_hex(kSdCols[i]), lv_color_hex(0xFFFFFF));
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
          if (!a->confirmTap(ctx->idx, "TAP AGAIN (5s): Confirm export private key to SD!")) {
            a->renderBody();
            return;
          }
          bool ok_s = (ctx->idx == 4) ? a->_actions.exportSettingsToSd(true) : true;
          bool ok_k = a->_actions.exportIdentityToSd();
          snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                   (ok_s && ok_k) ? "OK: Saved to /faro/ on MicroSD."
                                  : "ERR: Export to MicroSD failed (check SD card).");
        } else if (ctx->idx == 3) {
          if (!a->confirmTap(ctx->idx, "TAP AGAIN (5s): Replace node identity from SD?")) {
            a->renderBody();
            return;
          }
          if (a->_actions.importIdentityHex("SD")) {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "OK: Identity imported from MicroSD.");
          } else {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "ERR: /faro/identity.bak or prv_key.txt not found.");
          }
        } else if (ctx->idx == 5) {
          if (!a->confirmTap(ctx->idx, "TAP AGAIN (5s): Restore settings & identity from SD?")) {
            a->renderBody();
            return;
          }
          bool ok_s = a->_actions.importSettingsFromSd();
          bool ok_k = a->_actions.importIdentityHex("SD");
          if (ok_s || ok_k) {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "OK: Restored (%s%s) from MicroSD!",
                     ok_s ? "Settings" : "", ok_k ? (ok_s ? "+Keys" : "Keys") : "");
          } else {
            snprintf(s.sd_backup_status, sizeof(s.sd_backup_status),
                     "ERR: No backup files found in /faro/ on SD.");
          }
          a->rebuildChrome();
          a->updateStatusBar();
        }
        a->renderBody();
      }, LV_EVENT_CLICKED, &s_sdctx[i]);
      lv_obj_t* l = lv_label_create(b);
      lv_obj_set_style_text_font(l, &faro_font_12, 0);
      lv_label_set_text(l, armed_here ? "TAP AGAIN: Confirm!" : kSdBtns[i]);
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
        const char* const* kScopes = nullptr;
        const size_t n_scopes = scopeNames(a->_store.region(), &kScopes);
        size_t next = 0;
        for (size_t s = 0; s < n_scopes; ++s) {
          if (strcmp(a->_store.status().active_scope_name, kScopes[s]) == 0) {
            next = (s + 1) % n_scopes;
            break;
          }
        }
        a->_actions.setDefaultScope(kScopes[next]);
      } else if (ctx->idx == 1) {
        // Off-grid: client repeat on the region's repeat frequency (narrow 62.5 kHz, SF8 CR8)
        const RadioRegion& rg = a->_store.regionInfo();
        RadioConfig cfg = a->_store.radio();
        if (!cfg.client_repeat && rg.repeat_count == 0) {
          snprintf(a->_store.status().last_cli_reply, sizeof(a->_store.status().last_cli_reply),
                   "Off-grid: %s has no client-repeat frequency.", rg.code);
          a->renderBody();
          return;
        }
        if (!cfg.client_repeat) {
          cfg.freq_mhz = rg.repeat_freqs[rg.repeat_count > 2 ? 2 : 0];
          cfg.bw_khz = 62.5f;
          cfg.sf = 8;
          cfg.cr = 8;
        }
        cfg.client_repeat = !cfg.client_repeat;
        if (a->_actions.setRadioConfig(cfg)) {
          a->_actions.setDefaultScope(cfg.client_repeat ? "offgrid(*)" : "*");
        }
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


}  // namespace faro::ui
