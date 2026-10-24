// SPDX-License-Identifier: Apache-2.0
// sim/main_sdl.cpp — Native Linux simulator for Faro (interactive SDL2 + headless --dump-all PNG exporter)
#include <lvgl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(FARO_HAS_SDL2)
#include <SDL2/SDL.h>
#endif

#include "core/AppStore.h"
#include "core/EventBus.h"
#include "core/OfflineTileReader.h"
#include "ui/App.h"
#include "VirtualMeshActions.h"

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

uint16_t g_fb[kWidth * kHeight] = {0};
uint16_t g_draw_buf[kWidth * kHeight] = {0};

void simFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    if (y < 0 || y >= kHeight) continue;
    for (int32_t x = area->x1; x <= area->x2; ++x) {
      if (x >= 0 && x < kWidth) {
        g_fb[y * kWidth + x] = *src;
      }
      ++src;
    }
  }
  lv_display_flush_ready(disp);
}

bool writeFramebufferPng(const char* path, const uint16_t* fb_rgb565) {
  FILE* fp = fopen(path, "wb");
  if (!fp) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    fclose(fp);
    return false;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    fclose(fp);
    return false;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, kWidth, kHeight, 8,
               PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  uint8_t row_rgb[kWidth * 3];
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      uint16_t p = fb_rgb565[y * kWidth + x];
      uint8_t r5 = (p >> 11) & 0x1F;
      uint8_t g6 = (p >> 5) & 0x3F;
      uint8_t b5 = p & 0x1F;
      row_rgb[x * 3 + 0] = uint8_t((r5 << 3) | (r5 >> 2));
      row_rgb[x * 3 + 1] = uint8_t((g6 << 2) | (g6 >> 4));
      row_rgb[x * 3 + 2] = uint8_t((b5 << 3) | (b5 >> 2));
    }
    png_write_row(png, row_rgb);
  }

  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

void ensureDir(const char* dir) {
  mkdir(dir, 0755);
}

}  // namespace

int main(int argc, char** argv) {
  const char* dump_dir = nullptr;
  faro::core::Language lang = faro::core::Language::EN;
  bool outdoor = true;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--dump-all") == 0 && i + 1 < argc) {
      dump_dir = argv[++i];
    } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
      const char* l = argv[++i];
      if (strcmp(l, "IT") == 0 || strcmp(l, "it") == 0) lang = faro::core::Language::IT;
      else if (strcmp(l, "DE") == 0 || strcmp(l, "de") == 0) lang = faro::core::Language::DE;
      else if (strcmp(l, "FR") == 0 || strcmp(l, "fr") == 0) lang = faro::core::Language::FR;
      else lang = faro::core::Language::EN;
    } else if (strcmp(argv[i], "--outdoor") == 0) {
      outdoor = true;
    } else if (strcmp(argv[i], "--dark") == 0) {
      outdoor = false;
    }
  }

  lv_init();
  lv_display_t* disp = lv_display_create(kWidth, kHeight);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g_draw_buf, nullptr, sizeof(g_draw_buf), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, simFlushCb);

  faro::core::AppStore store;
  faro::core::MeshEventBus bus;
  faro::sim::VirtualMeshActions actions(store, bus);
  faro::core::OfflineTileReader tiles;

  actions.seedDemoTopology();
  store.uiPrefs().language = lang;
  store.uiPrefs().outdoor_high_contrast = outdoor;

  faro::ui::App app(store, actions, tiles);
  app.begin();

  if (dump_dir) {
    ensureDir(dump_dir);
    for (uint8_t s = 0; s < uint8_t(faro::ui::ScreenId::_Count); ++s) {
      auto scr_id = static_cast<faro::ui::ScreenId>(s);
      app.showScreen(scr_id);
      for (int step = 0; step < 5; ++step) {
        lv_tick_inc(20);
        lv_timer_handler();
      }
      lv_refr_now(disp);

      char out_path[512] = {0};
      snprintf(out_path, sizeof(out_path), "%s/%s.png",
               dump_dir, faro::ui::App::screenSlug(scr_id));
      if (!writeFramebufferPng(out_path, g_fb)) {
        fprintf(stderr, "ERROR: Failed to write %s\n", out_path);
        return 1;
      }
      printf("Dumped %s (%dx%d)\n", out_path, kWidth, kHeight);
    }

    // Also dump edition-specific Home views & Diagnostics sub-tabs for README gallery
    auto dumpNamed = [&](const char* name) {
      for (int step = 0; step < 5; ++step) {
        lv_tick_inc(20);
        lv_timer_handler();
      }
      lv_refr_now(disp);
      char out_path[512] = {0};
      snprintf(out_path, sizeof(out_path), "%s/%s.png", dump_dir, name);
      writeFramebufferPng(out_path, g_fb);
      printf("Dumped %s (%dx%d)\n", out_path, kWidth, kHeight);
    };

    app.showScreen(faro::ui::ScreenId::Home);
    dumpNamed("home");

    app.setDiagTab(2);
    app.showScreen(faro::ui::ScreenId::Diagnostics);
    dumpNamed("diag_graph");

    app.setDiagTab(3);
    app.showScreen(faro::ui::ScreenId::Diagnostics);
    dumpNamed("diag_rfcalc");

    app.setDiagTab(4);
    app.showScreen(faro::ui::ScreenId::Diagnostics);
    dumpNamed("diag_mesh");


    // Also dump Dark Theme previews (Google Material 3 Dark)
    store.uiPrefs().outdoor_high_contrast = false;
    app.showScreen(faro::ui::ScreenId::Home);
    dumpNamed("home_dark");

    app.showScreen(faro::ui::ScreenId::Map);
    dumpNamed("10_map_dark");
    store.uiPrefs().outdoor_high_contrast = outdoor;

    return 0;
  }

#if defined(FARO_HAS_SDL2)
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("Faro — LilyGO T-Deck Plus Simulator (320x240)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     kWidth * 2, kHeight * 2, SDL_WINDOW_SHOWN);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                       SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);

  static int16_t mouse_x = 0, mouse_y = 0;
  static bool mouse_down = false;

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, [](lv_indev_t*, lv_indev_data_t* data) {
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  });

  bool running = true;
  uint32_t last_tick = SDL_GetTicks();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        mouse_x = int16_t(e.button.x / 2);
        mouse_y = int16_t(e.button.y / 2);
        mouse_down = true;
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        mouse_x = int16_t(e.button.x / 2);
        mouse_y = int16_t(e.button.y / 2);
        mouse_down = false;
      } else if (e.type == SDL_MOUSEMOTION) {
        mouse_x = int16_t(e.motion.x / 2);
        mouse_y = int16_t(e.motion.y / 2);
      } else if (e.type == SDL_KEYDOWN) {
        SDL_Keycode sym = e.key.keysym.sym;
        if (sym == SDLK_ESCAPE) app.onKeyPress(27);
        else if (sym == SDLK_BACKSPACE) app.onKeyPress('\b');
        else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) app.onKeyPress('\r');
        else if (sym == SDLK_TAB) app.onKeyPress('\t');
        else if (sym >= 32 && sym < 127) app.onKeyPress(uint8_t(sym));
      }
    }

    uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    faro::core::MeshEvent mev{};
    while (bus.pop(mev)) {
      store.applyEvent(mev, now);
    }
    actions.tick(now);
    app.tick(now);
    lv_timer_handler();

    SDL_UpdateTexture(tex, nullptr, g_fb, kWidth * sizeof(uint16_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
#endif
  return 0;
}
