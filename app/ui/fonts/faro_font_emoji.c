// SPDX-License-Identifier: Apache-2.0
// app/ui/fonts/faro_font_emoji.c — Graphical 12x12 anti-aliased emoji fallback glyphs for LVGL 9.3
// Chains between faro_font_{12,14,16,mono_12} and lv_font_montserrat_{12,14,16}.

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * 12x12 pixel art icons encoded as 12 ASCII strings of 12 chars per glyph:
 *   '.' = 0x00 (transparent)
 *   '+' = 0x55 (33% alpha anti-alias edge)
 *   '*' = 0xAA (67% alpha mid-tone / shading)
 *   '#' = 0xFF (100% opaque stroke / fill)
 */
typedef struct {
  const char* rows[12];
} FaroEmojiBitmap;

enum {
  EMOJI_GID_NONE = 0,
  EMOJI_GID_ZERO_WIDTH = 1,
  EMOJI_GID_SMILE,       // 😀 😃 😄 😊 🙂 ☺
  EMOJI_GID_LAUGH,       // 😂 🤣 😆
  EMOJI_GID_COOL,        // 😎
  EMOJI_GID_WINK,        // 😉
  EMOJI_GID_SAD,         // 😢 😭 😔 ☹
  EMOJI_GID_ANGRY,       // 😡 😠
  EMOJI_GID_THINK,       // 🤔
  EMOJI_GID_THUMB_UP,    // 👍 👌
  EMOJI_GID_THUMB_DOWN,  // 👎
  EMOJI_GID_WAVE,        // 👋 🙏
  EMOJI_GID_HEART,       // ❤️ 💙 💚 🧡 ♥
  EMOJI_GID_FIRE,        // 🔥
  EMOJI_GID_CHECK,       // ✅ ✔️ ✔
  EMOJI_GID_WARN,        // ⚠️ ⚠ ❗
  EMOJI_GID_CROSS,       // ❌ ✖
  EMOJI_GID_SOS,         // 🆘
  EMOJI_GID_PIN,         // 📍 📌
  EMOJI_GID_ANTENNA,     // 📡 📶
  EMOJI_GID_BATTERY,     // 🔋
  EMOJI_GID_MOUNTAIN,    // ⛰️ 🏔️
  EMOJI_GID_STAR,        // 🎉 ⭐ ✨
  EMOJI_GID_ROCKET,      // 🚀
  EMOJI_GID_COUNT
};

static const FaroEmojiBitmap kEmojiBitmaps[EMOJI_GID_COUNT] = {
  [EMOJI_GID_NONE] = {{0}},
  [EMOJI_GID_ZERO_WIDTH] = {{0}},

  // 😀 Classic Smiley Face
  [EMOJI_GID_SMILE] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*......*#.",
    "+#..#..#..#+",
    "#...#..#...#",
    "#..........#",
    "#..#....#..#",
    "#..*#..#*..#",
    "+#..####..#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 😂 Laughing with Tears
  [EMOJI_GID_LAUGH] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*##..##*#.",
    "+#.#.#..#.#+",
    "#..........#",
    "#+########+#",
    "##*......*##",
    "#+*######*+#",
    "+#..*##*..#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 😎 Cool Sunglasses Face
  [EMOJI_GID_COOL] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*......*#.",
    "+##########+",
    "#####..#####",
    "#*###..###*#",
    "#..........#",
    "#...*....#.#",
    "+#...####.#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 😉 Winking Face
  [EMOJI_GID_WINK] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*......*#.",
    "+#.###.#..#+",
    "#......#...#",
    "#..........#",
    "#..#....#..#",
    "#..*#..#*..#",
    "+#..####..#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 😢 Sad / Crying Face
  [EMOJI_GID_SAD] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*......*#.",
    "+#..#..#..#+",
    "#...#..#...#",
    "#......*...#",
    "#...####.*.#",
    "#..#....#..#",
    "+#.#....#.#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 😡 Angry Face
  [EMOJI_GID_ANGRY] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*#....#*#.",
    "+#..#..#..#+",
    "#...#..#...#",
    "#..........#",
    "#...####...#",
    "#..#....#..#",
    "+#........#+",
    ".#*......*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 🤔 Thinking Face
  [EMOJI_GID_THINK] = {{
    "...+####+...",
    ".+##*..*##+.",
    ".#*##...#*#.",
    "+#..#..#..#+",
    "#..........#",
    "#...####...#",
    "#..........#",
    "#...###....#",
    "+#..#.#...#+",
    ".#*###...*#.",
    ".+##*..*##+.",
    "...+####+..."
  }},

  // 👍 Thumbs Up
  [EMOJI_GID_THUMB_UP] = {{
    ".....##.....",
    "....###.....",
    "....#.#.....",
    "...##.#.....",
    "...#..#####.",
    ".###..#...#.",
    ".#.#..####..",
    ".#.#..#...#.",
    ".#.#..####..",
    ".#.#..#..#..",
    ".###..####..",
    "............"
  }},

  // 👎 Thumbs Down
  [EMOJI_GID_THUMB_DOWN] = {{
    "............",
    ".###..####..",
    ".#.#..#..#..",
    ".#.#..####..",
    ".#.#..#...#.",
    ".#.#..####..",
    ".###..#...#.",
    "...#..#####.",
    "...##.#.....",
    "....#.#.....",
    "....###.....",
    ".....##....."
  }},

  // 👋 Waving Hand
  [EMOJI_GID_WAVE] = {{
    "...#.#.#....",
    "..##.#.##...",
    "..##.#.##.#.",
    ".###.#.####.",
    ".##########.",
    ".#########..",
    "..########..",
    "..#######...",
    "...######...",
    "....####....",
    "............",
    "............"
  }},

  // ❤️ Filled Heart
  [EMOJI_GID_HEART] = {{
    "............",
    ".+###..###+.",
    "+##########+",
    "############",
    "############",
    "############",
    "+##########+",
    ".+########+.",
    "..+######+..",
    "...+####+...",
    "....+##+....",
    ".....++....."
  }},

  // 🔥 Flame / Fire
  [EMOJI_GID_FIRE] = {{
    "......#+....",
    ".....###....",
    "....####+...",
    "...#####.#..",
    "..######.##.",
    ".##########.",
    ".###*..*###.",
    "###*....*###",
    "###..##..###",
    "+###*..*###+",
    ".+########+.",
    "...+####+..."
  }},

  // ✅ Checkmark Box
  [EMOJI_GID_CHECK] = {{
    ".+########+.",
    "+##########+",
    "##........##",
    "##.......###",
    "##......###.",
    "##.#...###..",
    "##.##.###...",
    "##..####....",
    "##...##.....",
    "##........##",
    "+##########+",
    ".+########+."
  }},

  // ⚠️ Warning Triangle
  [EMOJI_GID_WARN] = {{
    ".....##.....",
    "....####....",
    "....#**#....",
    "...##..##...",
    "...#.&&.#..." ,
    "..##.##.##..",
    "..##.##.##..",
    ".###.##.###.",
    ".###....###.",
    "#####..#####",
    "############",
    ".+########+."
  }},

  // ❌ Cross / X Mark
  [EMOJI_GID_CROSS] = {{
    "............",
    ".##......##.",
    ".###....###.",
    "..###..###..",
    "...######...",
    "....####....",
    "....####....",
    "...######...",
    "..###..###..",
    ".###....###.",
    ".##......##.",
    "............"
  }},

  // 🆘 SOS Badge
  [EMOJI_GID_SOS] = {{
    ".+########+.",
    "############",
    "#.##.##.##.#",
    "##...#.#.#..",
    "#.##.#.#.##.",
    "#..#.#.#..#.",
    "##.#.##.##.#",
    "############",
    "#...####...#",
    "#..#....#..#",
    "############",
    ".+########+."
  }},

  // 📍 Location Pin
  [EMOJI_GID_PIN] = {{
    "...+####+...",
    "..########..",
    ".###*..*###.",
    ".##......##.",
    ".##..##..##.",
    ".###*..*###.",
    "..########..",
    "...######...",
    "....####....",
    "....####....",
    ".....##.....",
    ".....++....."
  }},

  // 📡 Radio Antenna
  [EMOJI_GID_ANTENNA] = {{
    "..#......#..",
    ".#.#....#.#.",
    "#..#.##.#..#",
    "#.#..##..#.#",
    ".#...##...#.",
    "..#..##..#..",
    ".....##.....",
    "....####....",
    "....####....",
    "...##..##...",
    "..###..###..",
    ".####..####."
  }},

  // 🔋 Battery
  [EMOJI_GID_BATTERY] = {{
    "....####....",
    "..########..",
    "..#......#..",
    "..#.####.#..",
    "..#.####.#..",
    "..#......#..",
    "..#.####.#..",
    "..#.####.#..",
    "..#......#..",
    "..#.####.#..",
    "..########..",
    "............"
  }},

  // ⛰️ Alpine Mountain Peak
  [EMOJI_GID_MOUNTAIN] = {{
    ".....##.....",
    "....####....",
    "....#..#....",
    "...##..##.#.",
    "..###.###.##",
    "..########.#",
    ".###########",
    ".###########",
    "############",
    "############",
    "############",
    "............"
  }},

  // 🎉 / ⭐ Star
  [EMOJI_GID_STAR] = {{
    ".....##.....",
    ".....##.....",
    "....####....",
    ".##########.",
    "..########..",
    "...######...",
    "...######...",
    "..########..",
    "..###..###..",
    ".###....###.",
    ".##......##.",
    "............"
  }},

  // 🚀 Rocket
  [EMOJI_GID_ROCKET] = {{
    ".....##.....",
    "....####....",
    "...######...",
    "...##..##...",
    "...##..##...",
    "..########..",
    ".##########.",
    ".#.######.#.",
    "#..######..#",
    "...######...",
    "....#..#....",
    "...#....#..."
  }}
};

static uint32_t mapUnicodeToEmojiGid(uint32_t cp) {
  // Zero-width modifiers & variation selectors (U+FE0E, U+FE0F, U+200D, skin tones)
  if (cp == 0xFE0EU || cp == 0xFE0FU || cp == 0x200DU ||
      (cp >= 0x1F3FBU && cp <= 0x1F3FFU)) {
    return EMOJI_GID_ZERO_WIDTH;
  }

  switch (cp) {
    case 0x1F600U:  // 😀
    case 0x1F601U:  // 😁
    case 0x1F603U:  // 😃
    case 0x1F604U:  // 😄
    case 0x1F60AU:  // 😊
    case 0x1F642U:  // 🙂
    case 0x263AU:   // ☺
      return EMOJI_GID_SMILE;

    case 0x1F602U:  // 😂
    case 0x1F606U:  // 😆
    case 0x1F923U:  // 🤣
      return EMOJI_GID_LAUGH;

    case 0x1F60EU:  // 😎
      return EMOJI_GID_COOL;

    case 0x1F609U:  // 😉
      return EMOJI_GID_WINK;

    case 0x1F614U:  // 😔
    case 0x1F61EU:  // 😞
    case 0x1F622U:  // 😢
    case 0x1F62DU:  // 😭
    case 0x2639U:   // ☹
      return EMOJI_GID_SAD;

    case 0x1F620U:  // 😠
    case 0x1F621U:  // 😡
      return EMOJI_GID_ANGRY;

    case 0x1F914U:  // 🤔
      return EMOJI_GID_THINK;

    case 0x1F44DU:  // 👍
    case 0x1F44CU:  // 👌
      return EMOJI_GID_THUMB_UP;

    case 0x1F44EU:  // 👎
      return EMOJI_GID_THUMB_DOWN;

    case 0x1F44BU:  // 👋
    case 0x1F64FU:  // 🙏
      return EMOJI_GID_WAVE;

    case 0x2764U:   // ❤
    case 0x2665U:   // ♥
    case 0x1F499U:  // 💙
    case 0x1F49AU:  // 💚
    case 0x1F9E1U:  // 🧡
      return EMOJI_GID_HEART;

    case 0x1F525U:  // 🔥
      return EMOJI_GID_FIRE;

    case 0x2705U:   // ✅
    case 0x2714U:   // ✔
      return EMOJI_GID_CHECK;

    case 0x26A0U:   // ⚠
    case 0x2757U:   // ❗
    case 0x2753U:   // ❓
      return EMOJI_GID_WARN;

    case 0x274CU:   // ❌
    case 0x2716U:   // ✖
      return EMOJI_GID_CROSS;

    case 0x1F198U:  // 🆘
      return EMOJI_GID_SOS;

    case 0x1F4CDU:  // 📍
    case 0x1F4CCU:  // 📌
      return EMOJI_GID_PIN;

    case 0x1F4E1U:  // 📡
    case 0x1F4F6U:  // 📶
      return EMOJI_GID_ANTENNA;

    case 0x1F50BU:  // 🔋
      return EMOJI_GID_BATTERY;

    case 0x26F0U:   // ⛰
    case 0x1F3D4U:  // 🏔
      return EMOJI_GID_MOUNTAIN;

    case 0x1F389U:  // 🎉
    case 0x2B50U:   // ⭐
    case 0x2728U:   // ✨
      return EMOJI_GID_STAR;

    case 0x1F680U:  // 🚀
      return EMOJI_GID_ROCKET;

    default:
      break;
  }

  // Fallback for any other supplementary plane face/emoticon in U+1F600..U+1F64F or U+1F900..U+1F9FF
  if ((cp >= 0x1F600U && cp <= 0x1F64FU) || (cp >= 0x1F910U && cp <= 0x1F92FU)) {
    return EMOJI_GID_SMILE;
  }
  if (cp >= 0x1F300U && cp <= 0x1FAFFU) {
    return EMOJI_GID_STAR;
  }
  return EMOJI_GID_NONE;
}

static bool emoji_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                uint32_t unicode_letter, uint32_t unicode_letter_next) {
  (void)font;
  (void)unicode_letter_next;
  uint32_t gid = mapUnicodeToEmojiGid(unicode_letter);
  if (gid == EMOJI_GID_NONE) return false;

  memset(dsc_out, 0, sizeof(*dsc_out));
  if (gid == EMOJI_GID_ZERO_WIDTH) {
    dsc_out->adv_w = 0;
    dsc_out->box_w = 0;
    dsc_out->box_h = 0;
    dsc_out->ofs_x = 0;
    dsc_out->ofs_y = 0;
    dsc_out->stride = 0;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = 0;
    dsc_out->gid.index = gid;
    return true;
  }

  dsc_out->adv_w = 14;
  dsc_out->box_w = 12;
  dsc_out->box_h = 12;
  dsc_out->ofs_x = 1;
  dsc_out->ofs_y = 0;
  dsc_out->stride = 0;
  dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
  dsc_out->is_placeholder = 0;
  dsc_out->gid.index = gid;
  return true;
}

static const void* emoji_get_glyph_bitmap(lv_font_glyph_dsc_t* g_dsc, lv_draw_buf_t* draw_buf) {
  if (!g_dsc || !draw_buf || !draw_buf->data) return NULL;
  uint32_t gid = g_dsc->gid.index;
  if (gid <= EMOJI_GID_ZERO_WIDTH || gid >= EMOJI_GID_COUNT) return NULL;

  const FaroEmojiBitmap* bmp = &kEmojiBitmaps[gid];
  uint8_t* dst = draw_buf->data;
  uint32_t stride_out = lv_draw_buf_width_to_stride(12, LV_COLOR_FORMAT_A8);

  for (int y = 0; y < 12; ++y) {
    const char* row = bmp->rows[y];
    uint8_t* line = dst + ((uint32_t)y) * stride_out;
    for (int x = 0; x < 12; ++x) {
      char c = row ? row[x] : '.';
      uint8_t a = 0x00;
      if (c == '#') a = 0xFF;
      else if (c == '*' || c == '&') a = 0xB0;
      else if (c == '+') a = 0x58;
      line[x] = a;
    }
  }
  lv_draw_buf_flush_cache(draw_buf, NULL);
  return draw_buf;
}

const lv_font_t faro_font_emoji_12 = {
  .get_glyph_dsc = emoji_get_glyph_dsc,
  .get_glyph_bitmap = emoji_get_glyph_bitmap,
  .line_height = 16,
  .base_line = 3,
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = 0,
  .underline_thickness = 1,
  .dsc = NULL,
  .fallback = &lv_font_montserrat_12,
  .user_data = NULL,
};

const lv_font_t faro_font_emoji_14 = {
  .get_glyph_dsc = emoji_get_glyph_dsc,
  .get_glyph_bitmap = emoji_get_glyph_bitmap,
  .line_height = 18,
  .base_line = 4,
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = 0,
  .underline_thickness = 1,
  .dsc = NULL,
  .fallback = &lv_font_montserrat_14,
  .user_data = NULL,
};

const lv_font_t faro_font_emoji_16 = {
  .get_glyph_dsc = emoji_get_glyph_dsc,
  .get_glyph_bitmap = emoji_get_glyph_bitmap,
  .line_height = 21,
  .base_line = 5,
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = 0,
  .underline_thickness = 1,
  .dsc = NULL,
  .fallback = &lv_font_montserrat_16,
  .user_data = NULL,
};
