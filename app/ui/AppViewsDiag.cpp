// SPDX-License-Identifier: Apache-2.0
// app/ui/AppViewsDiag.cpp — the Diagnostics and Boot Wizard views (split out of App.cpp, ADR-018)
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

  static const char* kDTabs[6] = {"Stats", "RX", "Graph", "RF", "Mesh", "Info"};
  struct DTabCtx { App* app; uint8_t t; };
  static DTabCtx s_dtctx[6];
  const uint8_t num_tabs = 6;
  auto tabWidth = [](uint8_t) { return 48; };
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
    // TAB 1: RX log. MeshCore hands the UI decoded traffic only (messages, adverts, ACKs, traces, answers):
    // raw RF frames reach only a connected app (PUSH_CODE_LOG_RX_DATA), so this is not a raw sniffer.
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
    lv_label_set_text_fmt(cnt_lbl, "Received traffic (%zu)", _store.packetLogCount());

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
      lv_label_set_text_fmt(emp, "Waiting for messages, adverts, ACKs\n(%.3f MHz)...", double(r.freq_mhz));
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
    const float duty_pct = RegionPolicy::dutyCyclePercent(r.airtime_factor);
    const uint32_t max_hr = toa_chat_ms > 0.0f ? uint32_t(3600000.0f * duty_pct / 100.0f / toa_chat_ms) : 0;
#if defined(FARO_RADIO_LR1110)
    const char* chip = "LR1110";
#else
    const char* chip = "SX1262";
#endif

    lv_obj_t* rl = lv_label_create(rcard);
    lv_obj_set_pos(rl, 0, 0);
    lv_obj_set_width(rl, 296);
    lv_obj_set_style_text_font(rl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(rl, p.text_primary, 0);
    lv_label_set_text_fmt(rl,
                          "%s %.3fMHz BW%.1fk SF%u CR4/%u %ddBm\n"
                          "RX Sens: %.1fdBm | Link Budget: %.1fdB\n"
                          "ToA 64B Chat: %.0fms | 104B Adv: %.0fms\n"
                          "%.0f%% Duty: %u msg/h | RngTest: %zu pts",
                          chip, double(r.freq_mhz), double(r.bw_khz), unsigned(r.sf),
                          unsigned(r.cr), int(r.tx_power_dbm),
                          double(rx_sens_dbm), double(link_bgt_db),
                          double(toa_chat_ms), double(toa_adv_ms),
                          double(duty_pct), unsigned(max_hr), _store.rangeSampleCount());

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

    char pos_lines[96] = "Pos: no GPS fix";
    if (g.valid) {
      char ll[32];
      formatLatLon(g.lat_deg, g.lon_deg, 4, ll, sizeof(ll));
      int u_z = 0; char u_b = 'T'; int32_t u_e = 0, u_n = 0;
      geomath::wgs84ToUtm(g.lat_deg, g.lon_deg, u_z, u_b, u_e, u_n);
      if (up.coord_format == 1) {
        int32_t lv_e = 0, lv_n = 0;
        geomath::wgs84ToSwissLV95(g.lat_deg, g.lon_deg, lv_e, lv_n);
        snprintf(pos_lines, sizeof(pos_lines), "%s | LV95:%d,%d", ll, int(lv_e), int(lv_n));
      } else {
        snprintf(pos_lines, sizeof(pos_lines), "%s | UTM:%d%c %d %d", ll, u_z, u_b, int(u_e), int(u_n));
      }
    }

    static const char* kBotNames[3] = {"OFF", "PingBot", "AwayBot"};

    lv_obj_t* gl = lv_label_create(gcard);
    lv_obj_set_pos(gl, 0, 0);
    lv_obj_set_width(gl, 296);
    lv_obj_set_style_text_font(gl, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(gl, p.text_primary, 0);
    lv_label_set_text_fmt(gl,
                          "Bot:%s(%u) | S&F:%s(%u) | Vars:%.12s\n"
                          "%s\n%s",
                          kBotNames[up.bot_mode % 3], unsigned(up.bot_replies_sent),
                          up.store_and_forward ? "ON" : "OFF", unsigned(up.sf_delivered_count),
                          st.custom_vars_str, pos_lines,
                          st.export_status[0] ? st.export_status : "Exports go to the SD card (/faro/)");

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

  constexpr uint8_t kAboutTab = 5;
  if (_diag_tab == kAboutTab) {
    lv_obj_t* acard = lv_obj_create(parent);
    lv_obj_set_size(acard, 308, 138);
    Theme::styleCard(acard, outdoor);
    lv_obj_set_style_pad_all(acard, 4, 0);
    lv_obj_remove_flag(acard, LV_OBJ_FLAG_SCROLLABLE);

    char build[48] = {0};
    snprintf(build, sizeof(build), "%s %s", __DATE__, __TIME__);
    lv_obj_t* al = lv_label_create(acard);
    lv_obj_set_pos(al, 0, 0);
    lv_obj_set_width(al, 298);
    lv_label_set_long_mode(al, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(al, &faro_font_mono_12, 0);
    lv_obj_set_style_text_color(al, p.text_primary, 0);
    lv_label_set_text_fmt(al,
                          "Faro %s | Reset:%s\n"
                          "Build: %s\n"
                          "Heap:%uK PSRAM:%uK Up:%lus\n"
                          "SD:%s %u/%uMB\n"
                          "WiFi:%s %s\n"
                          "BLE PIN:%06u | C:%zu M:%zu\n"
                          "Loop:%u/%ums",
                          FARO_VERSION, resetReasonName(), build,
                          unsigned(st.free_heap / 1024), unsigned(st.free_psram / 1024),
                          (unsigned long)(st.uptime_secs),
                          st.sd_mounted ? "OK" : "---", unsigned(st.sd_used_mb),
                          unsigned(st.sd_total_mb),
                          st.wifi_connected ? "connected" : (st.wifi_enabled ? "offline" : "off"),
                          st.wifi_connected ? st.wifi_ip : "",
                          unsigned(st.ble_pin), _store.contactCount(), _store.messageCount(),
                          unsigned(st.last_loop_ms), unsigned(st.max_loop_ms));

    lv_obj_t* hint = lv_label_create(acard);
    lv_obj_set_pos(hint, 0, 108);
    lv_obj_set_width(hint, 298);
    lv_obj_set_style_text_font(hint, &faro_font_12, 0);
    lv_obj_set_style_text_color(hint, p.text_secondary, 0);
    lv_label_set_text(hint, "Space=Map | Esc=Home | long-press msg=del\ntype \"snake\" or \"tile\" for a game");
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
                        "Duty:%.1f%%/%.0f%%(%s) | Sparkline(16x):",
                        int(st.last_rssi), int(st.last_snr_q4) / 4, int(st.noise_floor_dbm),
                        unsigned(st.tx_packets), unsigned(st.direct_tx), unsigned(st.flood_tx),
                        unsigned(st.rx_packets), unsigned(st.queue_size),
                        unsigned(st.free_heap / 1024), unsigned(st.free_psram / 1024),
                        unsigned(st.last_loop_ms), unsigned(st.max_loop_ms),
                        double(st.duty_cycle_permille) / 10.0,
                        double(RegionPolicy::dutyCyclePercent(_store.radio().airtime_factor)),
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
  const auto& r = _store.radio();
  lv_label_set_text_fmt(info,
                        "1. %s: %s (%s) | Node: %s\n"
                        "2. Region: %s (max %d dBm)\n"
                        "3. Select Frequency / Preset:",
                        tr(StrId::LanguageLabel, lang), languageName(lang), languageCode(lang),
                        _store.status().node_name, reg.name,
                        int(RegionPolicy::maxConductedDbm(reg)));

  // Frequency / Preset Drop-Down List directly in BootWizard
  char dd_opts[512] = {0};
  size_t dd_pos = 0;
  for (uint8_t k = 0; k < reg.preset_count; ++k) {
    int n = snprintf(dd_opts + dd_pos, sizeof(dd_opts) - dd_pos, "%s%s (%.3f MHz)",
                     k > 0 ? "\n" : "", reg.presets[k].name, double(reg.presets[k].freq_mhz));
    if (n > 0 && dd_pos + size_t(n) < sizeof(dd_opts)) dd_pos += size_t(n);
  }
  const int pi = RegionPolicy::findPreset(reg, r.freq_mhz, r.bw_khz, r.sf, r.cr);

  lv_obj_t* pr_dd = lv_dropdown_create(card);
  lv_obj_set_size(pr_dd, 292, 28);
  lv_obj_set_pos(pr_dd, 0, 52);
  lv_dropdown_set_symbol(pr_dd, nullptr);
  lv_dropdown_set_options(pr_dd, dd_opts);
  lv_dropdown_set_selected(pr_dd, pi >= 0 ? uint32_t(pi) : 0U);
  lv_obj_set_style_text_font(pr_dd, &faro_font_12, 0);
  lv_obj_set_style_bg_color(pr_dd, p.surface_alt, 0);
  lv_obj_set_style_bg_opa(pr_dd, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(pr_dd, p.text_primary, 0);
  lv_obj_set_style_border_color(pr_dd, p.accent, 0);
  lv_obj_set_style_border_width(pr_dd, 1, 0);
  lv_obj_set_style_pad_ver(pr_dd, 4, 0);
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

  // Region: applied at once (frequency, power ceiling, duty cycle of the region)
  lv_obj_t* reg_btn = lv_button_create(card);
  lv_obj_set_size(reg_btn, 140, 34);
  lv_obj_set_pos(reg_btn, 0, 86);
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
  lv_obj_set_size(done_btn, 146, 34);
  lv_obj_set_pos(done_btn, 146, 86);
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
