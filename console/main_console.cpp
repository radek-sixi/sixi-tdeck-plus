// SPDX-License-Identifier: Apache-2.0
// console/main_console.cpp — Native SDL2 + LVGL 9 PC Desktop Console (faro-console) (ADR-016)
#include <lvgl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(FARO_HAS_SDL2)
#include <SDL2/SDL.h>
#endif

#include "core/AppStore.h"
#include "core/EventBus.h"
#include "core/OfflineTileReader.h"
#include "CompanionClientMeshActions.h"
#include "ui/App.h"
#include "VirtualMeshActions.h"

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

uint16_t g_fb[kWidth * kHeight] = {0};
uint16_t g_draw_buf[kWidth * kHeight] = {0};

void consoleFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
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

bool parseHexToken16(const char* hex, uint8_t out[16]) {
  return faro::core::AppStore::hexToBytes(hex, out, 16);
}

void printUsage(const char* argv0) {
  printf(
      "Usage: %s [options]\n"
      "Options:\n"
      "  --tcp <host:port>       Connect to remote node over TCP (default port 5000)\n"
      "  --usb <serial-device>   Connect to remote node over USB CDC serial (e.g. /dev/ttyACM0)\n"
      "  --baud <rate>           Serial baud rate (default 115200)\n"
      "  --light                 Use Google Material 3 Light theme (default)\n"
      "  --dark                  Use Google Material 3 Dark theme\n"
      "  --screenshot <file.png> Render one frame after sync and save PNG (headless)\n"
      "  --help                  Show this help message\n",
      argv0);
}

}  // namespace

int main(int argc, char** argv) {
  const char* tcp_target = nullptr;
  const char* usb_target = nullptr;
  const char* psk_hex = nullptr;
  const char* screenshot_path = nullptr;
  int baud = 115200;
  int theme_override = -1;  // -1 = keep store/default, 1 = Google Light, 0 = Google Dark

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--tcp") == 0 && i + 1 < argc) {
      tcp_target = argv[++i];
    } else if (strcmp(argv[i], "--usb") == 0 && i + 1 < argc) {
      usb_target = argv[++i];
    } else if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
      psk_hex = argv[++i];
    } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
      baud = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot_path = argv[++i];
    } else if (strcmp(argv[i], "--light") == 0 || strcmp(argv[i], "--outdoor") == 0) {
      theme_override = 1;
    } else if (strcmp(argv[i], "--dark") == 0) {
      theme_override = 0;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    }
  }

  lv_init();
  lv_display_t* disp = lv_display_create(kWidth, kHeight);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g_draw_buf, nullptr, sizeof(g_draw_buf), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, consoleFlushCb);

  faro::core::AppStore store;
  faro::core::MeshEventBus bus;
  faro::core::OfflineTileReader tiles;
  faro::platform::remote::CompanionClientMeshActions remote_actions(store, bus);
  faro::sim::VirtualMeshActions fallback_actions(store, bus);
  faro::core::IMeshActions* active_actions = &fallback_actions;

  char store_path[256] = "/tmp/faro_store.dat";
  if (const char* home = getenv("HOME")) {
    snprintf(store_path, sizeof(store_path), "%s/.faro_store.dat", home);
  }
  if (!screenshot_path) {
    store.loadFromFile(store_path);
  }
  if (theme_override != -1) {
    store.uiPrefs().outdoor_high_contrast = (theme_override == 1);
  }

  if (tcp_target) {
    char host[128] = {0};
    uint16_t port = 5000;
    const char* colon = strchr(tcp_target, ':');
    if (colon) {
      size_t hlen = size_t(colon - tcp_target);
      if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
      memcpy(host, tcp_target, hlen);
      port = uint16_t(atoi(colon + 1));
    } else {
      strncpy(host, tcp_target, sizeof(host) - 1);
    }

    uint8_t token16[16] = {0};
    const uint8_t* token_ptr = nullptr;
    if (psk_hex && parseHexToken16(psk_hex, token16)) {
      token_ptr = token16;
    }
    if (remote_actions.connectTcp(host, port, token_ptr)) {
      remote_actions.syncStore();
      active_actions = &remote_actions;
      printf("[faro-console] Connected via TCP to %s:%u | Node='%s' | Radio=%.3f MHz BW=%.1f kHz SF%u CR%u | Contacts=%zu\n",
             host, unsigned(port), store.status().node_name,
             double(store.radio().freq_mhz), double(store.radio().bw_khz),
             unsigned(store.radio().sf), unsigned(store.radio().cr),
             store.contactCount());
      fflush(stdout);
    } else {
      fprintf(stderr, "[faro-console] Failed to connect to %s:%u\n", host, unsigned(port));
      return 1;
    }
  } else if (usb_target) {
    if (remote_actions.connectSerial(usb_target, baud)) {
      remote_actions.syncStore();
      active_actions = &remote_actions;
      printf("[faro-console] Connected via USB serial to %s @ %d\n", usb_target, baud);
    } else {
      fprintf(stderr, "[faro-console] Failed to open serial port %s\n", usb_target);
      return 1;
    }
  } else {
    fallback_actions.seedDemoTopology();
  }

  faro::core::MeshEvent mev{};
  while (bus.pop(mev)) {
    store.applyEvent(mev, 100);
  }

  faro::ui::App app(store, *active_actions, tiles);
  app.begin();

  if (screenshot_path) {
    for (int step = 0; step < 5; ++step) {
      lv_tick_inc(20);
      lv_timer_handler();
    }
    lv_refr_now(disp);
    return writeFramebufferPng(screenshot_path, g_fb) ? 0 : 1;
  }

#if defined(FARO_HAS_SDL2)
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  constexpr int kScale = 3;
  const char* win_title = "Faro — MeshCore Companion UI [F11:Fullscreen]";
  SDL_Window* win = SDL_CreateWindow(win_title,
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     kWidth * kScale, kHeight * kScale,
                                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_RenderSetLogicalSize(ren, kWidth, kHeight);
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
  bool fullscreen = false;
  uint32_t last_tick = SDL_GetTicks();
  uint32_t last_save_ms = last_tick;
  uint32_t last_saved_ver = store.version();
  char last_clip[160] = {0};
  SDL_StartTextInput();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        mouse_x = int16_t(e.button.x);
        mouse_y = int16_t(e.button.y);
        mouse_down = true;
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        mouse_x = int16_t(e.button.x);
        mouse_y = int16_t(e.button.y);
        mouse_down = false;
      } else if (e.type == SDL_MOUSEMOTION) {
        mouse_x = int16_t(e.motion.x);
        mouse_y = int16_t(e.motion.y);
      } else if (e.type == SDL_TEXTINPUT) {
        char c = e.text.text[0];
        if (c >= 32 && c < 127) app.onKeyPress(uint8_t(c));
      } else if (e.type == SDL_KEYDOWN) {
        SDL_Keycode sym = e.key.keysym.sym;
        uint16_t mod = e.key.keysym.mod;
        if (sym == SDLK_F11) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        } else if ((mod & KMOD_CTRL) && sym == SDLK_v) {
          if (SDL_HasClipboardText()) {
            char* cb = SDL_GetClipboardText();
            if (cb) {
              strncpy(store.uiPrefs().clipboard, cb, sizeof(store.uiPrefs().clipboard) - 1);
              for (const char* p = cb; *p; ++p) {
                if (*p >= 32 && *p < 127) app.onKeyPress(uint8_t(*p));
              }
              SDL_free(cb);
            }
          }
        } else if ((mod & KMOD_CTRL) && sym == SDLK_c) {
          if (store.uiPrefs().clipboard[0]) {
            SDL_SetClipboardText(store.uiPrefs().clipboard);
          }
        } else if (sym == SDLK_ESCAPE) app.onKeyPress(27);
        else if (sym == SDLK_BACKSPACE) app.onKeyPress('\b');
        else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) app.onKeyPress('\r');
        else if (sym == SDLK_TAB) app.onKeyPress('\t');
      }
    }

    // Sync internal clipboard changes out to OS clipboard
    if (strcmp(last_clip, store.uiPrefs().clipboard) != 0 && store.uiPrefs().clipboard[0]) {
      strncpy(last_clip, store.uiPrefs().clipboard, sizeof(last_clip) - 1);
      SDL_SetClipboardText(store.uiPrefs().clipboard);
    }

    uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    if (active_actions == &remote_actions) {
      remote_actions.poll();
      store.tickMessageTimeouts(now);
      for (uint32_t id = 0; store.takeResendRequest(id);) remote_actions.resendDirectMessage(id);
    } else {
      fallback_actions.tick(now);
    }
    while (bus.pop(mev)) {
      store.applyEvent(mev, now);
    }
    app.tick(now);
    lv_timer_handler();

    if (store.version() != last_saved_ver && (now - last_save_ms) >= 2000) {
      store.saveToFile(store_path);
      last_saved_ver = store.version();
      last_save_ms = now;
    }

    SDL_UpdateTexture(tex, nullptr, g_fb, kWidth * sizeof(uint16_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  store.saveToFile(store_path);

  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
#endif
  return 0;
}
