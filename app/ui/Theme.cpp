// SPDX-License-Identifier: Apache-2.0
// app/ui/Theme.cpp — Google Material 3 Light (default) & Dark Tactical themes for 320x240 IPS (ADR-018)
#include "Theme.h"

namespace faro::ui {

namespace {
bool s_light_active = true;  // theme of the last palette() call; one theme is active at a time
}  // namespace

const ThemePalette& Theme::palette(bool outdoor_high_contrast) {
  // Google Material 3 Dark Palette ("Clean Night / Tactical")
  static const ThemePalette kDark = {
    lv_color_hex(0x131314),  // bg (Google Material 3 Dark Surface)
    lv_color_hex(0x1E1F20),  // surface (Google Elevated Dark Card & App Bar)
    lv_color_hex(0x282A2C),  // surface_alt (Google Dark Tonal Container for pills & tabs)
    lv_color_hex(0x444746),  // border (Google Dark Hairline Outline)
    lv_color_hex(0xE3E3E3),  // text_primary (Google Dark On-Surface)
    lv_color_hex(0xC4C7C5),  // text_secondary (Google Dark On-Surface Variant)
    lv_color_hex(0x1A73E8),  // accent (Google Blue 600: white text 4.5:1)
    lv_color_hex(0x34A853),  // success (Google Green 500)
    lv_color_hex(0xB06000),  // warning (Amber 900: white text 4.6:1)
    lv_color_hex(0xEA4335)   // danger (Google Red 500)
  };
  // Google Material 3 / Google UX Light Palette ("Clean Daylight")
  static const ThemePalette kGoogleLight = {
    lv_color_hex(0xF1F3F4),  // bg (Google Cool Gray 100 — soft canvas so white cards pop)
    lv_color_hex(0xFFFFFF),  // surface (Pure White Google Card & App Bar)
    lv_color_hex(0xE8F0FE),  // surface_alt (Google Tonal Blue 50 for pills, tabs & chips)
    lv_color_hex(0xDADCE0),  // border (Google Hairline Gray 300)
    lv_color_hex(0x202124),  // text_primary (Google Ink 900 — deep charcoal)
    lv_color_hex(0x5F6368),  // text_secondary (Google Neutral Gray 700)
    lv_color_hex(0x1A73E8),  // accent (Google Blue 600)
    lv_color_hex(0x188038),  // success (Google Green 700: white text 5:1)
    lv_color_hex(0xB06000),  // warning (Amber 900: white text 4.6:1)
    lv_color_hex(0xD93025)   // danger (Google Red 600)
  };
  s_light_active = outdoor_high_contrast;
  return outdoor_high_contrast ? kGoogleLight : kDark;
}

lv_color_t Theme::avatarColor(uint8_t key_byte) {
  static const uint32_t kColors[8] = {
    0x1A73E8, 0x1E8E3E, 0x9334E6, 0xE37400,
    0xD93025, 0x007B83, 0xC5221F, 0x185ABC
  };
  return lv_color_hex(kColors[key_byte & 0x07]);
}

void Theme::styleCard(lv_obj_t* obj, bool outdoor_high_contrast) {
  const auto& p = palette(outdoor_high_contrast);
  lv_obj_set_style_bg_color(obj, p.surface, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, p.border, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_pad_all(obj, 6, 0);
  styleFocusRing(obj);
}

void Theme::styleTouchButton(lv_obj_t* btn, lv_color_t bg_color, lv_color_t text_color) {
  const bool is_light_bg = (bg_color.red > 200 && bg_color.green > 200 && bg_color.blue > 200);
  if (is_light_bg && text_color.red > 240 && text_color.green > 240 && text_color.blue > 240) {
    // Automatically use Google Ink (#202124) when a button sits on a light tonal/white surface
    text_color = lv_color_hex(0x202124);
  }
  lv_obj_set_style_bg_color(btn, bg_color, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(btn, text_color, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_pad_hor(btn, 8, 0);
  lv_obj_set_style_pad_ver(btn, 4, 0);
  if (is_light_bg) {
    lv_obj_set_style_border_color(btn, lv_color_hex(0xD2E3FC), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
  } else if (bg_color.red < 55 && bg_color.green < 55 && bg_color.blue < 55) {
    lv_obj_set_style_border_color(btn, lv_color_hex(0x444746), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
  } else {
    lv_obj_set_style_border_width(btn, 0, 0);
  }
  styleFocusRing(btn);
}

void Theme::styleFocusRing(lv_obj_t* obj) {
  // The ring sits outside the widget, on the page background: ink on light, white on dark,
  // so trackball focus stays visible on accent-coloured buttons too.
  const lv_color_t ring = s_light_active ? lv_color_hex(0x202124) : lv_color_hex(0xFFFFFF);
  lv_obj_set_style_outline_color(obj, ring, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(obj, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(obj, ring, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(obj, 3, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUS_KEY);
}

}  // namespace faro::ui
