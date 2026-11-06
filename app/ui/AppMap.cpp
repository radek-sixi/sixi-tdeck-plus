// SPDX-License-Identifier: Apache-2.0
// app/ui/AppMap.cpp — map base/pan/zoom rendering and the Map view (split out of App.cpp, ADR-018)
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

void App::computeMapBase(double& out_lat, double& out_lon) {
  const auto& g = _store.gps();
  const auto& st = _store.status();

  // Without a GPS fix: the manual position, else the region's centre (or the SD tiles when the
  // card does not cover that centre at this zoom).
  out_lat = _store.regionInfo().map_lat;
  out_lon = _store.regionInfo().map_lon;
  double sd_lat = 0.0, sd_lon = 0.0;
  int sd_zoom = -1;
  const bool has_sd_default = _tiles.getDefaultTileCenter(sd_lat, sd_lon, sd_zoom);
  if (!_map_sd_synced && has_sd_default && sd_zoom >= 1 && sd_zoom <= 18) {
    _map_zoom = sd_zoom;
    _map_sd_synced = true;
  }

  if (g.valid) {
    out_lat = g.lat_deg;
    out_lon = g.lon_deg;
  } else if (st.manual_lat_deg != 0.0 || st.manual_lon_deg != 0.0) {
    out_lat = st.manual_lat_deg;
    out_lon = st.manual_lon_deg;
  } else if (has_sd_default) {
    int ch_tx = 0, ch_ty = 0, ch_px = 0, ch_py = 0;
    geomath::latLonToTilePixel(out_lat, out_lon, _map_zoom, ch_tx, ch_ty, ch_px, ch_py);
    if (!_tiles.hasTile(_map_zoom, ch_tx, ch_ty)) {
      out_lat = sd_lat;
      out_lon = sd_lon;
    }
  }
}

void App::renderMapCanvas() {
  double base_lat = 0.0, base_lon = 0.0;
  computeMapBase(base_lat, base_lon);
  const bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  _tiles.renderViewportRgb565(base_lat + _map_pan_lat_offset, base_lon + _map_pan_lon_offset,
                              _map_zoom, _map_canvas_buf, 308, 138, outdoor);
  _map_last_render_ms = lv_tick_get();
  _map_render_dirty = false;
  if (_map_canvas_obj) lv_obj_invalidate(_map_canvas_obj);
}

void App::buildMapView(lv_obj_t* parent) {
  bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  const auto& g = _store.gps();
  const auto& trk = _store.gpsTrack();
  const auto& st = _store.status();

  double base_lat = 0.0, base_lon = 0.0;
  computeMapBase(base_lat, base_lon);
  double clat = base_lat + _map_pan_lat_offset;
  double clon = base_lon + _map_pan_lon_offset;

  // Render slippy-tile / procedural viewport into _map_canvas_buf (308x138)
  renderMapCanvas();

  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, _map_canvas_buf, 308, 138, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_size(canvas, 308, 138);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  _map_canvas_obj = canvas;

  // Touch drag/pan directly on the map canvas
  lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (!self || !indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
      self->_map_drag_last_x = int16_t(pt.x);
      self->_map_drag_last_y = int16_t(pt.y);
      self->_map_drag_moved = false;
    } else if (code == LV_EVENT_PRESSING) {
      int16_t dx = int16_t(pt.x) - self->_map_drag_last_x;
      int16_t dy = int16_t(pt.y) - self->_map_drag_last_y;
      if (dx * dx + dy * dy >= 16) {
        self->_map_drag_last_x = int16_t(pt.x);
        self->_map_drag_last_y = int16_t(pt.y);
        self->_map_drag_moved = true;
        int z = self->_map_zoom < 1 ? 1 : (self->_map_zoom > 18 ? 18 : self->_map_zoom);
        double deg_per_px = 360.0 / double(256U << z);
        self->_map_pan_lon_offset -= double(dx) * deg_per_px;
        self->_map_pan_lat_offset += double(dy) * deg_per_px * 0.70;

        // Coalesce pan renders: touch delivers PRESSING far faster than the panel can be redrawn,
        // and a render may have to decode a tile. At most one render per 40 ms; the rest are
        // folded into the next App::tick().
        self->_map_render_dirty = true;
        const uint32_t now = lv_tick_get();
        if (now - self->_map_last_render_ms >= 40U) {
          self->renderMapCanvas();
        }
      }
    } else if (code == LV_EVENT_RELEASED) {
      if (self->_map_drag_moved || self->_map_render_dirty) {
        self->_map_drag_moved = false;
        self->_map_render_dirty = false;
        self->renderBody();  // refresh pins/HUD once, after the final canvas render
      }
    }
  }, LV_EVENT_ALL, this);

  // Overlay GPS Track info & elevation badge + [GPX] export if enabled (Ripple UI p.31-33)
  if (_show_tracks_overlay && trk.point_count > 0) {
    lv_obj_t* trk_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(trk_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(trk_lbl, p.success, 0);
    lv_obj_set_width(trk_lbl, 130);  // keep the top-centre clear for the north arrow
    lv_label_set_long_mode(trk_lbl, LV_LABEL_LONG_DOT);
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
  }
  // GPS/SD state lives on the bottom HUD bar now — the map itself stays clean.

  // Overlay self crosshair & contact pins on canvas
  lv_obj_t* self_pin = lv_label_create(canvas);
  lv_obj_set_style_text_font(self_pin, &faro_font_12, 0);
  lv_obj_set_style_text_color(self_pin, p.accent, 0);
  lv_label_set_text(self_pin, g.valid ? "[+] Me" : "[+]");
  lv_obj_set_pos(self_pin, 134, 64);

  int cx_t = 0, cy_t = 0, cx_px = 0, cy_px = 0;
  geomath::latLonToTilePixel(clat, clon, _map_zoom, cx_t, cy_t, cx_px, cy_px);
  int32_t world_cx = cx_t * 256 + cx_px;
  int32_t world_cy = cy_t * 256 + cy_px;

  int32_t used_sy[16] = {0};
  size_t used_count = 0;

  // Contact pins: show at most the 8 closest to the map centre; the rest collapse into a "+N"
  // badge so dozens of GPS contacts never bury the map in text.
  struct PinCand { size_t idx; double d2; };
  static PinCand s_cand[AppStore::kMaxContacts];
  size_t cand_n = 0;
  {
    const double cos_lat = cos(clat * 3.14159265358979323846 / 180.0);
    for (size_t i = 0; i < AppStore::kMaxContacts; ++i) {
      const auto& c = _store.contacts()[i];
      if (!c.valid || !c.hasGps()) continue;
      const double dlat = c.latDeg() - clat;
      const double dlon = (c.lonDeg() - clon) * cos_lat;
      s_cand[cand_n].idx = i;
      s_cand[cand_n].d2 = dlat * dlat + dlon * dlon;
      ++cand_n;
    }
    for (size_t i = 1; i < cand_n; ++i) {  // insertion sort, ascending distance
      const PinCand v = s_cand[i];
      size_t j = i;
      while (j > 0 && s_cand[j - 1].d2 > v.d2) {
        s_cand[j] = s_cand[j - 1];
        --j;
      }
      s_cand[j] = v;
    }
  }
  constexpr size_t kMaxMapPins = 8;
  const size_t shown = cand_n < kMaxMapPins ? cand_n : kMaxMapPins;
  const size_t hidden = cand_n - shown;

  for (size_t k = 0; k < shown; ++k) {
    const auto& c = _store.contacts()[s_cand[k].idx];
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

  if (hidden > 0) {
    lv_obj_t* more_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(more_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(more_lbl, p.text_secondary, 0);
    lv_label_set_text_fmt(more_lbl, "+%u more", unsigned(hidden));
    lv_obj_set_pos(more_lbl, 4, (_show_tracks_overlay && trk.point_count > 0) ? 16 : 2);
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

  // North arrow (top-centre): a white up-triangle on a black underlay, so it stays visible on
  // both the light and the high-contrast map palette.
  {
    static lv_point_precise_t n_bg[4] = {{146, 8}, {152, 0}, {158, 8}, {146, 8}};
    static lv_point_precise_t n_fg[4] = {{147, 7}, {152, 2}, {157, 7}, {147, 7}};
    lv_obj_t* n_bg_l = lv_line_create(canvas);
    lv_line_set_points(n_bg_l, n_bg, 4);
    lv_obj_set_style_line_width(n_bg_l, 3, 0);
    lv_obj_set_style_line_color(n_bg_l, lv_color_hex(0x000000), 0);
    lv_obj_t* n_fg_l = lv_line_create(canvas);
    lv_line_set_points(n_fg_l, n_fg, 4);
    lv_obj_set_style_line_width(n_fg_l, 2, 0);
    lv_obj_set_style_line_color(n_fg_l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t* n_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(n_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(n_lbl, lv_color_hex(0x000000), 0);
    lv_label_set_text(n_lbl, "N");
    lv_obj_set_pos(n_lbl, 152, 8);  // dark shadow
    lv_obj_t* n_lbl_fg = lv_label_create(canvas);
    lv_obj_set_style_text_font(n_lbl_fg, &faro_font_12, 0);
    lv_obj_set_style_text_color(n_lbl_fg, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(n_lbl_fg, "N");
    lv_obj_set_pos(n_lbl_fg, 151, 7);
  }

  // 40 px scale bar (bottom-left). Ground distance follows the Web-Mercator resolution of the
  // current zoom at the map centre latitude.
  {
    const double lat_rad = clat * 3.14159265358979323846 / 180.0;
    const double mpp = 156543.03392 * cos(lat_rad) / double(uint32_t(1U) << _map_zoom);
    const double dist_m = mpp * 40.0;
    char scale_txt[24] = {0};
    if (dist_m < 1000.0) {
      snprintf(scale_txt, sizeof(scale_txt), "~%dm", int(dist_m + 0.5));
    } else if (dist_m < 10000.0) {
      snprintf(scale_txt, sizeof(scale_txt), "~%.1fkm", dist_m / 1000.0);
    } else {
      snprintf(scale_txt, sizeof(scale_txt), "~%dkm", int(dist_m / 1000.0 + 0.5));
    }
    auto make_bar = [&](int x, int y, lv_coord_t width, lv_color_t color) {
      static lv_point_precise_t pts[4] = {{0, 0}, {40, 0}, {0, 0}, {0, 0}};
      pts[0].x = x; pts[0].y = y;
      pts[1].x = x + 40; pts[1].y = y;
      pts[2].x = x; pts[2].y = y - 4;
      pts[3].x = x; pts[3].y = y + 4;
      lv_obj_t* line = lv_line_create(canvas);
      lv_line_set_points(line, pts, 4);
      lv_obj_set_style_line_width(line, width, 0);
      lv_obj_set_style_line_color(line, color, 0);
      return line;
    };
    make_bar(6, 127, 4, lv_color_hex(0x000000));
    make_bar(6, 127, 2, lv_color_hex(0xFFFFFF));
    static lv_point_precise_t r_tick[2] = {{0, 0}, {0, 0}};
    r_tick[0].x = 46; r_tick[0].y = 123;
    r_tick[1].x = 46; r_tick[1].y = 131;
    lv_obj_t* rt_bg = lv_line_create(canvas);
    lv_line_set_points(rt_bg, r_tick, 2);
    lv_obj_set_style_line_width(rt_bg, 4, 0);
    lv_obj_set_style_line_color(rt_bg, lv_color_hex(0x000000), 0);
    lv_obj_t* rt_fg = lv_line_create(canvas);
    lv_line_set_points(rt_fg, r_tick, 2);
    lv_obj_set_style_line_width(rt_fg, 2, 0);
    lv_obj_set_style_line_color(rt_fg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t* s_lbl = lv_label_create(canvas);
    lv_obj_set_style_text_font(s_lbl, &faro_font_12, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0x000000), 0);
    lv_label_set_text(s_lbl, scale_txt);
    lv_obj_set_pos(s_lbl, 51, 121);  // dark shadow
    lv_obj_t* s_lbl_fg = lv_label_create(canvas);
    lv_obj_set_style_text_font(s_lbl_fg, &faro_font_12, 0);
    lv_obj_set_style_text_color(s_lbl_fg, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_lbl_fg, scale_txt);
    lv_obj_set_pos(s_lbl_fg, 50, 120);
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
    char ll[32];
    formatLatLon(clat, clon, 3, ll, sizeof(ll));
    lv_label_set_text_fmt(coord, "Z%d %s", _map_zoom, ll);
  }

  // Button 0 doubles as the map status: "GPS" (fix, SD tiles) / "GPS!" (fix, grid fallback) / "Ctr".
  const bool sd_tiles = _tiles.lastRenderUsedSdTiles();
  char gps_txt[8] = {0};
  snprintf(gps_txt, sizeof(gps_txt), "%s%s", g.valid ? "GPS" : "Ctr", sd_tiles ? "" : "!");
  const char* kMapBtns[3] = {gps_txt, "-", "+"};
  struct MapBtnCtx { App* app; int idx; };
  static MapBtnCtx s_mctx[3];
  for (int b = 0; b < 3; ++b) {
    lv_obj_t* mb = lv_button_create(hud);
    lv_obj_set_size(mb, b == 0 ? 44 : 36, 22);
    lv_color_t bg = p.surface_alt;
    lv_color_t fg = p.text_primary;
    if (b == 1 || b == 2) {
      bg = p.accent;
      fg = lv_color_hex(0xFFFFFF);
    } else if (g.valid) {
      bg = sd_tiles ? p.success : p.warning;  // fix OK vs no offline tiles
      fg = lv_color_hex(0xFFFFFF);
    }
    Theme::styleTouchButton(mb, bg, fg);
    s_mctx[b] = {this, b};
    lv_obj_add_event_cb(mb, [](lv_event_t* e) {
      auto* ctx = static_cast<MapBtnCtx*>(lv_event_get_user_data(e));
      App* a = ctx->app;
      if (ctx->idx == 0) {
        a->_map_pan_lat_offset = 0.0;
        a->_map_pan_lon_offset = 0.0;
      } else if (ctx->idx == 1) {
        a->_map_zoom = (a->_map_zoom <= 4) ? 17 : (a->_map_zoom - 1);
      } else if (ctx->idx == 2) {
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


}  // namespace faro::ui
