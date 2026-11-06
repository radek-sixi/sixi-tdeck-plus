// SPDX-License-Identifier: Apache-2.0
// app/ui/AppUiShared.h — free helpers shared by the split App*.cpp view files (ADR-018).
// All functions are inline so every translation unit gets its own copy (no ODR issues).
#pragma once

#include "App.h"
#include "Theme.h"
#include "fonts/FaroFonts.h"
#include "i18n/Strings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

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


inline const char* roleBadge(NodeRole r) {
  switch (r) {
    case NodeRole::Repeater:   return "RPT";
    case NodeRole::RoomServer: return "ROOM";
    case NodeRole::Sensor:     return "SENS";
    case NodeRole::ChatNode:
    default:                   return "CHAT";
  }
}

inline const char* deliveryBadge(DeliveryState s) {
  switch (s) {
    case DeliveryState::Queued:       return "...";
    case DeliveryState::SentDirect:   return "DIR>";
    case DeliveryState::SentFlood:    return "FLD~";
    case DeliveryState::DeliveredAck: return "ACK OK";
    case DeliveryState::Failed:       return "ERR!";
    default:                          return "";
  }
}

inline void makeInitials(const char* name, char out[3]) {
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

inline lv_obj_t* createAvatarCircle(lv_obj_t* parent, const char* name, uint8_t key_byte) {
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

// Translates common UTF-8 emoji sequences into readable ASCII emoticons/badges so messages from
// mobile apps render cleanly with the embedded 1-bit/4-bit bitmap fonts.
// Normalize incoming and outgoing text so supported emojis render as graphical glyphs via
// faro_font_emoji_12/14/16 fallback fonts, and common ASCII emoticons upgrade to graphical emojis.
inline void formatEmojiText(const char* in, char* out, size_t max_len) {
  if (!out || max_len == 0) return;
  if (!in) {
    out[0] = '\0';
    return;
  }
  struct EmojiMap {
    const char* pattern;
    const char* repl;
  };
  // Map synonymous UTF-8 emojis and common ASCII emoticons to glyphs present in faro_font_emoji
  static const EmojiMap kMap[] = {
    // Multi-byte UTF-8 variants with U+FE0F variation selector or sister codepoints
    {"\xE2\x9D\xA4\xEF\xB8\x8F", "\xE2\x9D\xA4"},         // ❤️ -> ❤ (U+2764)
    {"\xF0\x9F\x92\x99", "\xE2\x9D\xA4"},                 // 💙 -> ❤
    {"\xF0\x9F\x92\x9A", "\xE2\x9D\xA4"},                 // 💚 -> ❤
    {"\xE2\x9A\xA0\xEF\xB8\x8F", "\xE2\x9A\xA0"},         // ⚠️ -> ⚠ (U+26A0)
    {"\xE2\x9C\x94\xEF\xB8\x8F", "\xE2\x9C\x85"},         // ✔️ -> ✅ (U+2705)
    {"\xE2\x9C\x94", "\xE2\x9C\x85"},                     // ✔ -> ✅ (U+2705)
    {"\xE2\x9B\xB0\xEF\xB8\x8F", "\xE2\x9B\xB0"},         // ⛰️ -> ⛰ (U+26F0)
    {"\xF0\x9F\x98\x83", "\xF0\x9F\x98\x80"},             // 😃 -> 😀 (U+1F600)
    {"\xF0\x9F\x98\x84", "\xF0\x9F\x98\x80"},             // 😄 -> 😀
    {"\xF0\x9F\x98\x81", "\xF0\x9F\x98\x80"},             // 😁 -> 😀
    {"\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x80"},             // 😊 -> 😀
    {"\xF0\x9F\x99\x82", "\xF0\x9F\x98\x80"},             // 🙂 -> 😀
    {"\xF0\x9F\xA4\xA3", "\xF0\x9F\x98\x82"},             // 🤣 -> 😂 (U+1F602)
    {"\xF0\x9F\x98\x94", "\xF0\x9F\x98\xA2"},             // 😔 -> 😢 (U+1F622)
    {"\xF0\x9F\x98\xAD", "\xF0\x9F\x98\xA2"},             // 😭 -> 😢
    {"\xF0\x9F\x91\x8C", "\xF0\x9F\x91\x8D"},             // 👌 -> 👍 (U+1F44D)
    {"\xF0\x9F\x99\x8F", "\xF0\x9F\x91\x8D"},             // 🙏 -> 👍
    // Common ASCII emoticons -> graphical UTF-8 emoji glyphs
    {":-)", "\xF0\x9F\x98\x80"},                          // :-) -> 😀
    {":)", "\xF0\x9F\x98\x80"},                           // :)  -> 😀
    {":-D", "\xF0\x9F\x98\x82"},                          // :-D -> 😂
    {":D", "\xF0\x9F\x98\x82"},                           // :D  -> 😂
    {";-)", "\xF0\x9F\x98\x89"},                          // ;-) -> 😉
    {";)", "\xF0\x9F\x98\x89"},                           // ;)  -> 😉
    {"B-)", "\xF0\x9F\x98\x8E"},                          // B-) -> 😎
    {":-(", "\xF0\x9F\x98\xA2"},                          // :-( -> 😢
    {":(", "\xF0\x9F\x98\xA2"},                           // :(  -> 😢
    {"<3", "\xE2\x9D\xA4"},                               // <3  -> ❤
  };

  // Supported 4-byte UTF-8 emojis rendered directly by faro_font_emoji_12/14/16
  static const char* const kSupported4Byte[] = {
    "\xF0\x9F\x86\x98",  // 🆘 U+1F198
    "\xF0\x9F\x8E\x89",  // 🎉 U+1F389
    "\xF0\x9F\x91\x8B",  // 👋 U+1F44B
    "\xF0\x9F\x91\x8D",  // 👍 U+1F44D
    "\xF0\x9F\x91\x8E",  // 👎 U+1F44E
    "\xF0\x9F\x93\x8D",  // 📍 U+1F4CD
    "\xF0\x9F\x93\xA1",  // 📡 U+1F4E1
    "\xF0\x9F\x94\x8B",  // 🔋 U+1F50B
    "\xF0\x9F\x94\xA5",  // 🔥 U+1F525
    "\xF0\x9F\x98\x80",  // 😀 U+1F600
    "\xF0\x9F\x98\x82",  // 😂 U+1F602
    "\xF0\x9F\x98\x89",  // 😉 U+1F609
    "\xF0\x9F\x98\x8E",  // 😎 U+1F60E
    "\xF0\x9F\x98\xA1",  // 😡 U+1F621
    "\xF0\x9F\x98\xA2",  // 😢 U+1F622
    "\xF0\x9F\x9A\x80",  // 🚀 U+1F680
    "\xF0\x9F\xA4\x94",  // 🤔 U+1F914
  };

  size_t o = 0;
  const char* p = in;
  while (*p && o + 1 < max_len) {
    bool matched = false;
    for (const auto& m : kMap) {
      size_t ulen = strlen(m.pattern);
      if (strncmp(p, m.pattern, ulen) == 0) {
        size_t rlen = strlen(m.repl);
        if (o + rlen < max_len) {
          memcpy(out + o, m.repl, rlen);
          o += rlen;
        }
        p += ulen;
        matched = true;
        break;
      }
    }
    if (matched) continue;
    // Strip Variation Selector-16 (U+FE0F = EF B8 8F) if left over
    if (static_cast<uint8_t>(p[0]) == 0xEF &&
        static_cast<uint8_t>(p[1]) == 0xB8 &&
        static_cast<uint8_t>(p[2]) == 0x8F) {
      p += 3;
      continue;
    }
    // Pass supported 4-byte supplementary plane emojis directly to LVGL emoji fallback font
    if (static_cast<uint8_t>(p[0]) == 0xF0 &&
        (static_cast<uint8_t>(p[1]) & 0xF0) == 0x90 &&
        p[1] && p[2] && p[3]) {
      bool is_supported = false;
      for (const char* sup : kSupported4Byte) {
        if (memcmp(p, sup, 4) == 0) {
          is_supported = true;
          break;
        }
      }
      if (is_supported) {
        if (o + 4 < max_len) {
          memcpy(out + o, p, 4);
          o += 4;
        }
      } else if (o + 4 < max_len) {
        // Unknown supplementary emoji -> render generic 😀 graphical icon instead of ASCII [*]
        memcpy(out + o, "\xF0\x9F\x98\x80", 4);
        o += 4;
      }
      p += 4;
      continue;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
}

inline lv_obj_t* createHeaderRow(lv_obj_t* parent, const char* title, bool outdoor,
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
  lv_obj_set_flex_align(hdr, back_cb ? LV_FLEX_ALIGN_SPACE_BETWEEN : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  if (back_cb) {
    lv_obj_t* back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 48, 26);
    Theme::styleTouchButton(back_btn, p.accent, lv_color_hex(0xFFFFFF));
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t* bl = lv_label_create(back_btn);
    lv_obj_set_style_text_font(bl, &faro_font_12, 0);
    lv_label_set_text(bl, "<Back");
    lv_obj_center(bl);

    if (user_data) {
      lv_obj_t* home_btn = lv_button_create(hdr);
      lv_obj_set_size(home_btn, 46, 26);
      Theme::styleTouchButton(home_btn, p.surface, p.accent);
      lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        auto* self = static_cast<App*>(lv_event_get_user_data(e));
        if (self) self->showScreen(ScreenId::Home);
      }, LV_EVENT_CLICKED, user_data);
      lv_obj_t* hl = lv_label_create(home_btn);
      lv_obj_set_style_text_font(hl, &faro_font_12, 0);
      lv_label_set_text(hl, "Home");
      lv_obj_center(hl);
    }
  }

  lv_obj_t* t_lbl = lv_label_create(hdr);
  lv_obj_set_width(t_lbl, back_cb ? 124 : 296);
  lv_obj_set_style_text_align(t_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(t_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(t_lbl, p.text_primary, 0);
  lv_label_set_text(t_lbl, title);
  lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_CLIP);
  return hdr;
}

// "46.818N 8.228E" style, with the hemisphere instead of a sign
inline void formatLatLon(double lat, double lon, int decimals, char* out, size_t cap) {
  snprintf(out, cap, "%.*f%c %.*f%c", decimals, lat < 0 ? -lat : lat, lat < 0 ? 'S' : 'N', decimals,
           lon < 0 ? -lon : lon, lon < 0 ? 'W' : 'E');
}

// Next TX power step at or below the region's ceiling (wraps to the lowest)
inline int8_t nextTxPower(int8_t cur, int8_t cap) {
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

// Flood scopes offered by the Scope button
inline size_t scopeNames(RadioRegionId region, const char* const** out) {
  static const char* const kEu[] = {"*", "#eu-mesh", "#ch-alps", "offgrid"};
  static const char* const kOther[] = {"*", "offgrid"};
  if (region == RadioRegionId::EU868) {
    *out = kEu;
    return sizeof(kEu) / sizeof(kEu[0]);
  }
  *out = kOther;
  return sizeof(kOther) / sizeof(kOther[0]);
}
inline const char* resetReasonName() {
#if defined(ARDUINO)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt WDT";
    case ESP_RST_TASK_WDT:  return "task WDT";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
#else
  return "n/a (host)";
#endif
}

inline bool containsKeywordIgnoreCase(const char* haystack, const char* needle) {
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

inline void drawCanvasPixel(uint16_t* buf, int w, int h, int x, int y, uint16_t rgb565) {
  if (x >= 0 && x < w && y >= 0 && y < h) {
    buf[y * w + x] = rgb565;
  }
}

inline void fillCanvasRect(uint16_t* buf, int w, int h, int x, int y, int rw, int rh, uint16_t rgb565) {
  if (!buf || rw <= 0 || rh <= 0) return;
  const int x0 = x < 0 ? 0 : x;
  const int y0 = y < 0 ? 0 : y;
  const int x1 = (x + rw) > w ? w : (x + rw);
  const int y1 = (y + rh) > h ? h : (y + rh);
  for (int py = y0; py < y1; ++py) {
    uint16_t* row = buf + (py * w);
    for (int px = x0; px < x1; ++px) row[px] = rgb565;
  }
}

inline void drawCanvasLine(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t rgb565) {
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

inline void drawCanvasCircle(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t rgb565) {
  for (int deg = 0; deg < 360; deg += 3) {
    double rad = double(deg) * 3.14159265358979323846 / 180.0;
    int x = cx + int(double(r) * cos(rad));
    int y = cy + int(double(r) * sin(rad));
    drawCanvasPixel(buf, w, h, x, y, rgb565);
  }
}


}  // namespace faro::ui
