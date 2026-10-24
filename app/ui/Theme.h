// SPDX-License-Identifier: Apache-2.0
// app/ui/Theme.h — Dark Tactical & Outdoor Sunlight themes for 320x240 IPS (ADR-018)
#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace faro::ui {

struct ThemePalette {
  lv_color_t bg;
  lv_color_t surface;
  lv_color_t surface_alt;
  lv_color_t border;
  lv_color_t text_primary;
  lv_color_t text_secondary;
  lv_color_t accent;
  lv_color_t success;
  lv_color_t warning;
  lv_color_t danger;
};

class Theme {
public:
  static const ThemePalette& palette(bool outdoor_high_contrast);
  static lv_color_t avatarColor(uint8_t key_byte);
  static void styleCard(lv_obj_t* obj, bool outdoor_high_contrast);
  static void styleTouchButton(lv_obj_t* btn, lv_color_t bg_color, lv_color_t text_color);
  static void styleFocusRing(lv_obj_t* obj);
};

}  // namespace faro::ui
