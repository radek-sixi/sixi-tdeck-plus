// SPDX-License-Identifier: Apache-2.0
// app/ui/AppViewsMesh.cpp — Discover, Contacts, ContactDetail, TracePath views (split out of App.cpp, ADR-018)
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
  static DiscNodeCtx s_dn_ctx[kMaxListRows];
  size_t* s_dorder = _order;
  const uint8_t df = _discover_filter;
  const size_t n_disc = sortedContacts(s_dorder, AppStore::kMaxContacts, [&](const ContactRecord& c) {
    if (df == 1 && c.role != NodeRole::Repeater) return false;
    if (df == 2 && c.role != NodeRole::RoomServer) return false;
    if (df == 3 && c.role != NodeRole::ChatNode) return false;
    if (df == 4 && c.role != NodeRole::Sensor) return false;
    return true;
  });
  for (size_t k = 0; k < n_disc && k < kMaxListRows; ++k) {
    const size_t i = s_dorder[k];
    const auto& c = _store.contacts()[i];

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 38);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_dn_ctx[k] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<DiscNodeCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_dn_ctx[k]);

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
    }, LV_EVENT_CLICKED, &s_dn_ctx[k]);
    lv_obj_t* bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "Select");
    lv_obj_center(bl);
  }
  if (n_disc > kMaxListRows) {
    lv_obj_t* more = lv_label_create(parent);
    lv_obj_set_style_text_font(more, &faro_font_12, 0);
    lv_obj_set_style_text_color(more, p.text_secondary, 0);
    lv_label_set_text_fmt(more, "+%u more: use a filter", unsigned(n_disc - kMaxListRows));
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
  static ContactCtx s_cctx[kMaxListRows];
  static ContactCtx s_fav_ctx[kMaxListRows];
  size_t* s_corder = _order;
  const UiPrefs& sup = _store.uiPrefs();
  const uint8_t cf = _contact_filter;
  const size_t n_match = sortedContacts(s_corder, AppStore::kMaxContacts, [&](const ContactRecord& c) {
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
    const size_t i = s_corder[k];
    const auto& c = _store.contacts()[i];

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 308, 40);
    Theme::styleCard(row, outdoor);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_cctx[k] = {this, i};
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      auto* ctx = static_cast<ContactCtx*>(lv_event_get_user_data(e));
      if (ctx && ctx->app) {
        ctx->app->_contact_detail_tab = 0;
        ctx->app->openContactDetail(ctx->idx);
      }
    }, LV_EVENT_CLICKED, &s_cctx[k]);

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

    s_fav_ctx[k] = {this, i};
    lv_obj_t* fbtn = lv_button_create(row);
    lv_obj_set_size(fbtn, 38, 28);
    Theme::styleTouchButton(fbtn, c.favourite ? p.warning : p.surface_alt, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(fbtn, [](lv_event_t* e) {
      auto* ctx = static_cast<ContactCtx*>(lv_event_get_user_data(e));
      auto& peer = ctx->app->_store.contactsMutable()[ctx->idx];
      peer.favourite = !peer.favourite;
      ctx->app->renderBody();
    }, LV_EVENT_CLICKED, &s_fav_ctx[k]);
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
    }, LV_EVENT_CLICKED, &s_cctx[k]);
    lv_obj_t* bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "Detail");
    lv_obj_center(bl);
  }
  if (n_match > kMaxListRows) {
    lv_obj_t* more = lv_label_create(parent);
    lv_obj_set_style_text_font(more, &faro_font_12, 0);
    lv_obj_set_style_text_color(more, p.text_secondary, 0);
    lv_label_set_text_fmt(more, "+%u more: use a filter or search", unsigned(n_match - kMaxListRows));
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
    char ll[40];
    formatLatLon(c->latDeg(), c->lonDeg(), 4, ll, sizeof(ll));
    char extra[40] = {0};
    if (c->remote_batt_mv > 0) {
      snprintf(extra, sizeof(extra), "\nBatt %.2fV", double(c->remote_batt_mv) / 1000.0);
    }
    lv_label_set_text_fmt(info,
                          "%s [%s] #%02X\nKey: %s...\nPath: %s (%u hops)\nGPS: %s%s",
                          c->name, roleBadge(c->role), unsigned(c->pub_key[0]), hex_pre,
                          c->hasPath() ? "Direct" : "Flood", unsigned(c->pathHops()),
                          c->hasGps() ? ll : "-", extra);

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


}  // namespace faro::ui
