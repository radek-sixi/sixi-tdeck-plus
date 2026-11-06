// SPDX-License-Identifier: Apache-2.0
// app/ui/AppGames.cpp — Snake and 2048 easter eggs + the phone WiFi-setup modal (split out of App.cpp, ADR-018)
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

void App::snakeSpawnFood() {
  if (_snake_len >= kSnakeMax) {
    _snake_game_over = true;
    return;
  }
  uint8_t rnd[2] = {0};
  for (int tries = 0; tries < kSnakeMax * 4; ++tries) {
    core::fillRandom(rnd, sizeof(rnd));
    const uint8_t idx = uint8_t((uint16_t(rnd[0]) | (uint16_t(rnd[1]) << 8)) % kSnakeMax);
    bool used = false;
    for (uint8_t i = 0; i < _snake_len; ++i) {
      if (_snake_cells[i] == idx) { used = true; break; }
    }
    if (!used) {
      _snake_food = idx;
      return;
    }
  }
  _snake_game_over = true;  // board is full
}

void App::snakeReset() {
  _snake_len = 3;
  const uint8_t cx = kSnakeCols / 2;
  const uint8_t cy = kSnakeRows / 2;
  _snake_cells[0] = uint8_t(cy * kSnakeCols + cx);
  _snake_cells[1] = uint8_t(cy * kSnakeCols + cx - 1);
  _snake_cells[2] = uint8_t(cy * kSnakeCols + cx - 2);
  _snake_dir = SnakeDir::Right;
  _snake_next_dir = SnakeDir::Right;
  _snake_score = 0;
  _snake_step_ms = 170;
  _snake_game_over = false;
  _snake_paused = false;
  _snake_last_step_ms = 0;
  snakeSpawnFood();
  renderSnakeCanvas();
  updateSnakeHud();
}

void App::snakeSetDirection(SnakeDir dir) {
  if (_snake_game_over || _snake_paused) return;
  const bool opposite =
      (dir == SnakeDir::Up && _snake_dir == SnakeDir::Down) ||
      (dir == SnakeDir::Down && _snake_dir == SnakeDir::Up) ||
      (dir == SnakeDir::Left && _snake_dir == SnakeDir::Right) ||
      (dir == SnakeDir::Right && _snake_dir == SnakeDir::Left);
  if (!opposite) _snake_next_dir = dir;
}

void App::snakeStep() {
  if (_snake_game_over || _snake_paused || _snake_len == 0) return;

  _snake_dir = _snake_next_dir;
  int hx = _snake_cells[0] % kSnakeCols;
  int hy = _snake_cells[0] / kSnakeCols;
  switch (_snake_dir) {
    case SnakeDir::Up:    --hy; break;
    case SnakeDir::Down:  ++hy; break;
    case SnakeDir::Left:  --hx; break;
    case SnakeDir::Right: ++hx; break;
  }
  hx = (hx + kSnakeCols) % kSnakeCols;  // wrap: friendlier with a swiped thumb
  hy = (hy + kSnakeRows) % kSnakeRows;
  const uint8_t new_head = uint8_t(hy * kSnakeCols + hx);
  const bool ate = (new_head == _snake_food);

  // The tail vacates its cell in the same step, so hitting it is not a collision unless we grow.
  const uint8_t check_len = uint8_t(_snake_len - (ate ? 0 : 1));
  for (uint8_t i = 0; i < check_len; ++i) {
    if (_snake_cells[i] == new_head) {
      _snake_game_over = true;
      if (_snake_score > _snake_best) _snake_best = _snake_score;
      updateSnakeHud();
      return;
    }
  }

  uint8_t new_len = uint8_t(_snake_len + (ate ? 1 : 0));
  if (new_len > kSnakeMax) new_len = kSnakeMax;
  for (int i = new_len - 1; i > 0; --i) _snake_cells[i] = _snake_cells[i - 1];
  _snake_cells[0] = new_head;
  _snake_len = new_len;

  if (ate) {
    _snake_score = uint16_t(_snake_score + 10);
    if (_snake_score > _snake_best) _snake_best = _snake_score;
    // 170 ms at the start, then a gentle ramp to 80 ms.
    _snake_step_ms = (_snake_score >= 200) ? 80 : uint32_t(170 - int(_snake_score / 10) * 4);
    if (_snake_len >= kSnakeMax) {
      _snake_game_over = true;
    } else {
      snakeSpawnFood();
    }
  }

  renderSnakeCanvas();
  updateSnakeHud();
}

void App::snakeTick(uint32_t now_ms) {
  if (_snake_game_over || _snake_paused || _snake_len == 0) {
    _snake_last_step_ms = now_ms;
    return;
  }
  if (_snake_last_step_ms == 0) {
    _snake_last_step_ms = now_ms;
    return;
  }
  if (now_ms - _snake_last_step_ms < _snake_step_ms) return;
  _snake_last_step_ms = now_ms;
  snakeStep();
}

void App::renderSnakeCanvas() {
  const bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  const uint16_t bg = lv_color_to_u16(p.bg);
  const uint16_t board = lv_color_to_u16(p.surface);
  const uint16_t grid = lv_color_to_u16(p.border);
  const uint16_t body = lv_color_to_u16(p.success);
  const uint16_t head = lv_color_to_u16(p.accent);
  const uint16_t food = lv_color_to_u16(p.danger);

  fillCanvasRect(_map_canvas_buf, 308, 138, 0, 0, 308, 138, bg);
  const int bx = (308 - kSnakeCols * kSnakeCellPx) / 2;
  const int by = (138 - kSnakeRows * kSnakeCellPx) / 2;
  fillCanvasRect(_map_canvas_buf, 308, 138, bx - 1, by - 1,
                 kSnakeCols * kSnakeCellPx + 2, kSnakeRows * kSnakeCellPx + 2, grid);
  fillCanvasRect(_map_canvas_buf, 308, 138, bx, by,
                 kSnakeCols * kSnakeCellPx, kSnakeRows * kSnakeCellPx, board);
  for (int c = 0; c <= kSnakeCols; ++c) {
    fillCanvasRect(_map_canvas_buf, 308, 138, bx + c * kSnakeCellPx, by, 1,
                   kSnakeRows * kSnakeCellPx, grid);
  }
  for (int r = 0; r <= kSnakeRows; ++r) {
    fillCanvasRect(_map_canvas_buf, 308, 138, bx, by + r * kSnakeCellPx,
                   kSnakeCols * kSnakeCellPx, 1, grid);
  }

  const int fx = bx + (_snake_food % kSnakeCols) * kSnakeCellPx;
  const int fy = by + (_snake_food / kSnakeCols) * kSnakeCellPx;
  fillCanvasRect(_map_canvas_buf, 308, 138, fx + 2, fy + 2,
                 kSnakeCellPx - 4, kSnakeCellPx - 4, food);

  for (uint8_t i = 0; i < _snake_len; ++i) {
    const int cx = bx + (_snake_cells[i] % kSnakeCols) * kSnakeCellPx;
    const int cy = by + (_snake_cells[i] / kSnakeCols) * kSnakeCellPx;
    fillCanvasRect(_map_canvas_buf, 308, 138, cx + 1, cy + 1,
                   kSnakeCellPx - 2, kSnakeCellPx - 2, i == 0 ? head : body);
  }

  if (_snake_canvas) lv_obj_invalidate(_snake_canvas);
}

void App::updateSnakeHud() {
  if (_snake_score_lbl) {
    lv_label_set_text_fmt(_snake_score_lbl, "Score %u  Best %u",
                          unsigned(_snake_score), unsigned(_snake_best));
  }
  if (_snake_pause_lbl) {
    lv_label_set_text(_snake_pause_lbl, _snake_paused ? "Resume" : "Pause");
  }
  if (_snake_status_lbl) {
    const char* text = "";
    if (_snake_game_over) {
      text = (_snake_len >= kSnakeMax) ? "YOU WIN!\nPress New" : "GAME OVER\nPress New";
    } else if (_snake_paused) {
      text = "PAUSED";
    }
    lv_label_set_text(_snake_status_lbl, text);
    if (text[0]) lv_obj_remove_flag(_snake_status_lbl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_snake_status_lbl, LV_OBJ_FLAG_HIDDEN);
  }
}

void App::buildGameView(lv_obj_t* parent) {
  const bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  lv_obj_set_style_pad_row(parent, 3, 0);

  lv_obj_t* hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, 308, 16);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = lv_label_create(hdr);
  lv_obj_set_style_text_font(title, &faro_font_14, 0);
  lv_obj_set_style_text_color(title, p.accent, 0);
  lv_label_set_text(title, "SNAKE");
  _snake_score_lbl = lv_label_create(hdr);
  lv_obj_set_style_text_font(_snake_score_lbl, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(_snake_score_lbl, p.text_primary, 0);

  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_obj_set_size(canvas, 308, 138);
  lv_canvas_set_buffer(canvas, _map_canvas_buf, 308, 138, LV_COLOR_FORMAT_RGB565);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  _snake_canvas = canvas;

  _snake_status_lbl = lv_label_create(canvas);
  lv_obj_set_style_text_font(_snake_status_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(_snake_status_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(_snake_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(_snake_status_lbl, p.bg, 0);
  lv_obj_set_style_bg_opa(_snake_status_lbl, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(_snake_status_lbl, 5, 0);
  lv_obj_set_style_radius(_snake_status_lbl, 6, 0);
  lv_obj_center(_snake_status_lbl);
  lv_obj_add_flag(_snake_status_lbl, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
      self->_snake_drag_x = int16_t(pt.x);
      self->_snake_drag_y = int16_t(pt.y);
    } else if (code == LV_EVENT_RELEASED) {
      const int16_t dx = int16_t(pt.x) - self->_snake_drag_x;
      const int16_t dy = int16_t(pt.y) - self->_snake_drag_y;
      if (abs(dx) < 10 && abs(dy) < 10) return;
      if (abs(dx) > abs(dy)) {
        self->snakeSetDirection(dx > 0 ? SnakeDir::Right : SnakeDir::Left);
      } else {
        self->snakeSetDirection(dy > 0 ? SnakeDir::Down : SnakeDir::Up);
      }
    } else if (code == LV_EVENT_CLICKED) {
      if (self->_snake_game_over) {
        self->snakeReset();
      } else if (self->_snake_paused) {
        self->_snake_paused = false;
        self->_snake_last_step_ms = 0;
        self->updateSnakeHud();
      }
    }
  }, LV_EVENT_ALL, this);

  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, 308, 26);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* pause_btn = lv_button_create(row);
  lv_obj_set_size(pause_btn, 96, 26);
  Theme::styleTouchButton(pause_btn, p.surface_alt, p.text_primary);
  _snake_pause_lbl = lv_label_create(pause_btn);
  lv_obj_set_style_text_font(_snake_pause_lbl, &faro_font_12, 0);
  lv_label_set_text(_snake_pause_lbl, "Pause");
  lv_obj_center(_snake_pause_lbl);
  lv_obj_add_event_cb(pause_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self || self->_snake_game_over) return;
    self->_snake_paused = !self->_snake_paused;
    self->_snake_last_step_ms = 0;
    self->updateSnakeHud();
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* new_btn = lv_button_create(row);
  lv_obj_set_size(new_btn, 96, 26);
  Theme::styleTouchButton(new_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_t* newl = lv_label_create(new_btn);
  lv_obj_set_style_text_font(newl, &faro_font_12, 0);
  lv_label_set_text(newl, "New");
  lv_obj_center(newl);
  lv_obj_add_event_cb(new_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) self->snakeReset();
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* exit_btn = lv_button_create(row);
  lv_obj_set_size(exit_btn, 96, 26);
  Theme::styleTouchButton(exit_btn, p.surface_alt, p.danger);
  lv_obj_t* exitl = lv_label_create(exit_btn);
  lv_obj_set_style_text_font(exitl, &faro_font_12, 0);
  lv_label_set_text(exitl, "Exit");
  lv_obj_center(exitl);
  lv_obj_add_event_cb(exit_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ScreenId::Home);
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* hint = lv_label_create(parent);
  lv_obj_set_style_text_font(hint, &faro_font_12, 0);
  lv_obj_set_style_text_color(hint, p.text_secondary, 0);
  lv_label_set_text(hint, "Swipe or W/A/S/D | Space pause | Esc exit");

  if (_snake_len == 0) snakeReset();
  else {
    renderSnakeCanvas();
    updateSnakeHud();
  }
}

void App::new2048() {
  for (size_t i = 0; i < 16; ++i) _2048_grid[i] = 0;
  _2048_score = 0;
  _2048_over = false;
  _2048_won = false;
  spawn2048();
  spawn2048();
  update2048();
}

void App::spawn2048() {
  uint8_t empty[16];
  size_t n = 0;
  for (size_t i = 0; i < 16; ++i) {
    if (_2048_grid[i] == 0) empty[n++] = uint8_t(i);
  }
  if (n == 0) return;
  uint8_t rnd[2] = {0};
  core::fillRandom(rnd, sizeof(rnd));
  const size_t idx = empty[rnd[0] % n];
  _2048_grid[idx] = (rnd[1] % 10 == 0) ? 4 : 2;
}

void App::move2048(int dir) {
  if (_2048_over) return;
  bool moved = false;
  for (int i = 0; i < 4; ++i) {
    int line[4] = {0};
    for (int j = 0; j < 4; ++j) {
      if (dir == 0) line[j] = _2048_grid[j * 4 + i];
      else if (dir == 1) line[j] = _2048_grid[(3 - j) * 4 + i];
      else if (dir == 2) line[j] = _2048_grid[i * 4 + j];
      else line[j] = _2048_grid[i * 4 + (3 - j)];
    }
    int packed[4] = {0};
    int o = 0;
    for (int j = 0; j < 4; ++j) if (line[j]) packed[o++] = line[j];
    int merged[4] = {0};
    int m = 0;
    int j = 0;
    while (j < 4) {
      if (j + 1 < 4 && packed[j] && packed[j] == packed[j + 1]) {
        merged[m] = packed[j] * 2;
        _2048_score = uint16_t(_2048_score + uint16_t(merged[m]));
        ++m;
        j += 2;
      } else {
        merged[m++] = packed[j++];
      }
    }
    for (int j = 0; j < 4; ++j) {
      uint16_t* cell = nullptr;
      if (dir == 0) cell = &_2048_grid[j * 4 + i];
      else if (dir == 1) cell = &_2048_grid[(3 - j) * 4 + i];
      else if (dir == 2) cell = &_2048_grid[i * 4 + j];
      else cell = &_2048_grid[i * 4 + (3 - j)];
      if (*cell != uint16_t(merged[j])) moved = true;
      *cell = uint16_t(merged[j]);
    }
  }
  if (moved) {
    if (_2048_score > _2048_best) _2048_best = _2048_score;
    bool full = true;
    bool can_move = false;
    bool reached = false;
    for (size_t i = 0; i < 16; ++i) {
      if (_2048_grid[i] == 0) full = false;
      if (_2048_grid[i] >= 2048) reached = true;
    }
    for (int r = 0; r < 4 && !can_move; ++r) {
      for (int c = 0; c < 4 && !can_move; ++c) {
        const uint16_t v = _2048_grid[r * 4 + c];
        if (v == 0) continue;
        if (c + 1 < 4 && _2048_grid[r * 4 + c + 1] == v) can_move = true;
        if (r + 1 < 4 && _2048_grid[(r + 1) * 4 + c] == v) can_move = true;
      }
    }
    if (reached) {
      _2048_won = true;
      _2048_over = true;
    } else if (full && !can_move) {
      _2048_over = true;
    }
    spawn2048();
  }
  update2048();
}

void App::update2048() {
  if (_2048_score_lbl) {
    lv_label_set_text_fmt(_2048_score_lbl, "Score %u  Best %u",
                          unsigned(_2048_score), unsigned(_2048_best));
  }
  if (_2048_status_lbl) {
    const char* text = _2048_over ? (_2048_won ? "YOU WIN!" : "GAME OVER") : "";
    lv_label_set_text(_2048_status_lbl, text);
    if (text[0]) lv_obj_remove_flag(_2048_status_lbl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_2048_status_lbl, LV_OBJ_FLAG_HIDDEN);
  }
  static const uint32_t kColors[13] = {
    0x161B22, 0xEEE4DA, 0xEDE0C8, 0xF2B179, 0xF59563, 0xF67C5F, 0xF65E3B,
    0xEDCF72, 0xEDCC61, 0xEDC850, 0xEDC53F, 0xEDC22E, 0x3C3A32};
  static const uint16_t kValues[12] = {0, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
  for (size_t i = 0; i < 16; ++i) {
    if (!_2048_tiles[i]) continue;
    const uint16_t v = _2048_grid[i];
    size_t ci = 12;
    for (size_t k = 0; k < 12; ++k) {
      if (v == kValues[k]) { ci = k; break; }
    }
    lv_obj_set_style_bg_color(_2048_tiles[i], lv_color_hex(kColors[ci]), 0);
    if (lv_obj_get_child_count(_2048_tiles[i]) > 0) {
      lv_obj_t* lbl = lv_obj_get_child(_2048_tiles[i], 0);
      if (v == 0) {
        lv_label_set_text(lbl, "");
      } else {
        lv_label_set_text_fmt(lbl, "%u", unsigned(v));
        lv_obj_set_style_text_color(lbl, lv_color_hex(v <= 4 ? 0x776E65U : 0xFFFFFFU), 0);
      }
    }
  }
  // LVGL 9.3 coalesces style-only changes on already-rendered rounded objects; the partial
  // invalidation from set_style alone does not repaint the tiles (seen on host and it would hit
  // the device the same way). A full-screen invalidation is ~1 ms on the 320x240 panel and
  // guarantees every move is visible immediately.
  lv_obj_invalidate(lv_screen_active());
}

void App::buildGame2048View(lv_obj_t* parent) {
  const bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  lv_obj_set_style_pad_row(parent, 3, 0);

  lv_obj_t* hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, 308, 16);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = lv_label_create(hdr);
  lv_obj_set_style_text_font(title, &faro_font_14, 0);
  lv_obj_set_style_text_color(title, p.accent, 0);
  lv_label_set_text(title, "2048");
  _2048_score_lbl = lv_label_create(hdr);
  lv_obj_set_style_text_font(_2048_score_lbl, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(_2048_score_lbl, p.text_primary, 0);

  lv_obj_t* board = lv_obj_create(parent);
  lv_obj_set_size(board, 145, 145);
  lv_obj_set_style_bg_color(board, p.surface_alt, 0);
  lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(board, p.border, 0);
  lv_obj_set_style_border_width(board, 1, 0);
  lv_obj_set_style_pad_all(board, 0, 0);
  lv_obj_remove_flag(board, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      lv_obj_t* tile = lv_obj_create(board);
      lv_obj_set_size(tile, 34, 34);
      lv_obj_set_pos(tile, c * 37, r * 37);
      lv_obj_set_style_radius(tile, 4, 0);
      lv_obj_set_style_bg_color(tile, p.surface, 0);
      lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
      lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* tl = lv_label_create(tile);
      lv_obj_set_style_text_font(tl, &faro_font_mono_12, 0);
      lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), 0);
      lv_obj_center(tl);
      _2048_tiles[r * 4 + c] = tile;
    }
  }

  _2048_status_lbl = lv_label_create(board);
  lv_obj_set_style_text_font(_2048_status_lbl, &faro_font_14, 0);
  lv_obj_set_style_text_color(_2048_status_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(_2048_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(_2048_status_lbl, p.bg, 0);
  lv_obj_set_style_bg_opa(_2048_status_lbl, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(_2048_status_lbl, 5, 0);
  lv_obj_set_style_radius(_2048_status_lbl, 6, 0);
  lv_obj_center(_2048_status_lbl);
  lv_obj_add_flag(_2048_status_lbl, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(board, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
      self->_2048_drag_x = int16_t(pt.x);
      self->_2048_drag_y = int16_t(pt.y);
    } else if (code == LV_EVENT_RELEASED) {
      const int16_t dx = int16_t(pt.x) - self->_2048_drag_x;
      const int16_t dy = int16_t(pt.y) - self->_2048_drag_y;
      if (abs(dx) < 10 && abs(dy) < 10) return;
      if (abs(dx) > abs(dy)) self->move2048(dx > 0 ? 3 : 2);
      else self->move2048(dy > 0 ? 1 : 0);
    } else if (code == LV_EVENT_CLICKED) {
      if (self->_2048_over) self->new2048();
    }
  }, LV_EVENT_ALL, this);

  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, 308, 24);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* new_btn = lv_button_create(row);
  lv_obj_set_size(new_btn, 140, 24);
  Theme::styleTouchButton(new_btn, p.success, lv_color_hex(0xFFFFFF));
  lv_obj_t* newl = lv_label_create(new_btn);
  lv_obj_set_style_text_font(newl, &faro_font_12, 0);
  lv_label_set_text(newl, "New");
  lv_obj_center(newl);
  lv_obj_add_event_cb(new_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) self->new2048();
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* exit_btn = lv_button_create(row);
  lv_obj_set_size(exit_btn, 140, 24);
  Theme::styleTouchButton(exit_btn, p.surface_alt, p.danger);
  lv_obj_t* exitl = lv_label_create(exit_btn);
  lv_obj_set_style_text_font(exitl, &faro_font_12, 0);
  lv_label_set_text(exitl, "Exit");
  lv_obj_center(exitl);
  lv_obj_add_event_cb(exit_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ScreenId::Home);
  }, LV_EVENT_CLICKED, this);

  lv_obj_t* hint = lv_label_create(parent);
  lv_obj_set_style_text_font(hint, &faro_font_12, 0);
  lv_obj_set_style_text_color(hint, p.text_secondary, 0);
  lv_label_set_text(hint, "Swipe or W/A/S/D | N new | Q exit");

  if (_2048_grid[0] == 0 && _2048_grid[15] == 0) new2048();
  else update2048();
}

void App::buildWifiApModal() {
  const bool outdoor = _store.uiPrefs().outdoor_high_contrast;
  const auto& p = Theme::palette(outdoor);
  const auto& st = _store.status();
  if (!st.wifi_ap_active) return;

  lv_obj_t* m = lv_obj_create(_body);
  lv_obj_set_size(m, 264, 148);
  lv_obj_align(m, LV_ALIGN_CENTER, 0, 0);
  lv_obj_move_foreground(m);
  Theme::styleCard(m, outdoor);
  lv_obj_set_style_pad_all(m, 6, 0);
  lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(m);
  lv_obj_set_pos(title, 0, 0);
  lv_obj_set_style_text_font(title, &faro_font_14, 0);
  lv_obj_set_style_text_color(title, p.accent, 0);
  lv_label_set_text(title, "WiFi setup via phone");

  lv_obj_t* cred = lv_label_create(m);
  lv_obj_set_pos(cred, 0, 22);
  lv_obj_set_width(cred, 250);
  lv_obj_set_style_text_font(cred, &faro_font_mono_12, 0);
  lv_obj_set_style_text_color(cred, p.text_primary, 0);
  lv_label_set_text_fmt(cred, "SSID: %s\nPwd: %s\nOpen: http://%s",
                        st.wifi_ap_ssid, st.wifi_ap_pwd, st.wifi_ap_ip);

  lv_obj_t* hint = lv_label_create(m);
  lv_obj_set_pos(hint, 0, 84);
  lv_obj_set_width(hint, 250);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(hint, &faro_font_12, 0);
  lv_obj_set_style_text_color(hint, p.text_secondary, 0);
  lv_label_set_text(hint, "Connect your phone to this WiFi and open the address. It stops by itself in 5 min.");

  lv_obj_t* stop_btn = lv_button_create(m);
  lv_obj_set_size(stop_btn, 118, 24);
  lv_obj_set_pos(stop_btn, 0, 116);
  Theme::styleTouchButton(stop_btn, p.danger, lv_color_hex(0xFFFFFF));
  lv_obj_add_event_cb(stop_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_actions.stopWifiSetupAp();
    self->_show_wifi_ap_modal = false;
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* stopl = lv_label_create(stop_btn);
  lv_obj_set_style_text_font(stopl, &faro_font_12, 0);
  lv_label_set_text(stopl, "Stop");
  lv_obj_center(stopl);

  lv_obj_t* close_btn = lv_button_create(m);
  lv_obj_set_size(close_btn, 118, 24);
  lv_obj_set_pos(close_btn, 128, 116);
  Theme::styleTouchButton(close_btn, p.surface_alt, p.accent);
  lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_show_wifi_ap_modal = false;  // keep the AP running
    self->renderBody();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* closel = lv_label_create(close_btn);
  lv_obj_set_style_text_font(closel, &faro_font_12, 0);
  lv_label_set_text(closel, "Close");
  lv_obj_center(closel);
}


}  // namespace faro::ui
