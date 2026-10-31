// SPDX-License-Identifier: Apache-2.0
// SdCard.cpp — MicroSD card helper on T-Deck Plus shared SPI bus (CS=39) (ADR-007, ADR-009)
#include "SdCard.h"
#include "core/GeoMath.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <driver/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

namespace faro::hal {

#ifdef ARDUINO
namespace {
bool isDigitsOnly(const char* s) {
  if (!s || !s[0]) return false;
  for (size_t i = 0; s[i]; ++i) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

const char* baseNameOf(const char* path) {
  if (!path) return "";
  const char* slash = strrchr(path, '/');
  return slash ? (slash + 1) : path;
}
}  // namespace
#endif

bool SdCard::begin() {
#if defined(ARDUINO) && defined(LILYGO_TDECK)
  pinMode(9, OUTPUT);
  digitalWrite(9, HIGH);
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);
  pinMode(11, OUTPUT);
  digitalWrite(11, HIGH);
  pinMode(kPinCs, OUTPUT);
  digitalWrite(kPinCs, HIGH);

  // IMPORTANT: On LilyGo T-Deck Plus, SX1262 LoRa (`target.cpp`, CS=9), ST7789 TFT (CS=12),
  // and MicroSD (CS=39) all share SCLK=40, MISO=38, MOSI=41 on FSPI (`SPI`).
  SPI.begin(kPinSclk, kPinMiso, kPinMosi, -1);
  pinMode(kPinMiso, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)kPinMiso);
  bool mounted = SD.begin(kPinCs, SPI);
  if (!mounted) {
    // Cleanly reset SPI bus and send dummy clocks with CS=HIGH to recover card state
    SD.end();
    SPI.end();
    delay(10);
    digitalWrite(9, HIGH);
    digitalWrite(12, HIGH);
    digitalWrite(kPinCs, HIGH);
    SPI.begin(kPinSclk, kPinMiso, kPinMosi, -1);
    pinMode(kPinMiso, INPUT_PULLUP);
    gpio_pullup_en((gpio_num_t)kPinMiso);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 16; ++i) SPI.transfer(0xFF);
    SPI.endTransaction();
    mounted = SD.begin(kPinCs, SPI);
  }
  if (!mounted) {
    digitalWrite(kPinCs, HIGH);
    pinMode(12, OUTPUT);
    digitalWrite(12, HIGH);
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    pinMode(kPinMiso, INPUT_PULLUP);
    gpio_pullup_en((gpio_num_t)kPinMiso);
    _mounted = false;
    return false;
  }
  uint8_t card_type = SD.cardType();
  if (card_type == CARD_NONE) {
    digitalWrite(kPinCs, HIGH);
    _mounted = false;
    return false;
  }
  _mounted = true;
  _total_mb = uint32_t(SD.totalBytes() / (1024ULL * 1024ULL));
  _used_mb  = uint32_t(SD.usedBytes() / (1024ULL * 1024ULL));
  ensureFaroDirs();
  discoverMapTiles();
  digitalWrite(kPinCs, HIGH);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
  Serial.printf("[FARO-SD] Mounted %u MB (used %u MB), tile_root='%s' ext='%s' zoom_mask=0x%05X def=%d/%d/%d\n",
                unsigned(_total_mb), unsigned(_used_mb), _tile_root, _tile_ext,
                unsigned(_avail_zoom_mask), _default_tile_z, _default_tile_x, _default_tile_y);
  return true;
#else
  _mounted = false;
  return false;
#endif
}

bool SdCard::probeTileRoot(const char* root_dir) {
#ifdef ARDUINO
  if (!_mounted || !root_dir) return false;
  const bool is_slash_root = (strcmp(root_dir, "/") == 0 || root_dir[0] == '\0');
  if (!is_slash_root && !SD.exists(root_dir)) return false;

  uint32_t zmask = 0;
  int found_z = -1, found_x = -1, found_y = -1;
  char found_ext[12] = {0};

  // Prefer zoom levels around 12..8 for initial default view
  static const int kZoomOrder[19] = {12, 11, 13, 10, 14, 9, 8, 15, 7, 6, 16, 5, 4, 17, 3, 2, 18, 1, 0};
  for (int zi = 0; zi < 19; ++zi) {
    int z = kZoomOrder[zi];
    char zpath[96] = {0};
    if (is_slash_root) {
      snprintf(zpath, sizeof(zpath), "/%d", z);
    } else {
      snprintf(zpath, sizeof(zpath), "%s/%d", root_dir, z);
    }
    if (!SD.exists(zpath)) continue;
    File zdir = SD.open(zpath);
    if (!zdir || !zdir.isDirectory()) {
      if (zdir) zdir.close();
      continue;
    }

    // Check up to 64 X subdirectories inside this zoom folder
    int checked_x = 0;
    while (checked_x < 64) {
      File xentry = zdir.openNextFile();
      if (!xentry) break;
      bool is_dir = xentry.isDirectory();
      const char* xname = baseNameOf(xentry.name());
      if (is_dir && isDigitsOnly(xname)) {
        int xval = atoi(xname);
        ++checked_x;
        int checked_y = 0;
        while (checked_y < 64) {
          File yentry = xentry.openNextFile();
          if (!yentry) break;
          if (!yentry.isDirectory()) {
            const char* yname = baseNameOf(yentry.name());
            const char* dot = strrchr(yname, '.');
            if (dot && dot != yname) {
              char ystem[16] = {0};
              size_t slen = size_t(dot - yname);
              if (slen < sizeof(ystem)) {
                memcpy(ystem, yname, slen);
                if (isDigitsOnly(ystem)) {
                  const char* ext = dot + 1;
                  if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "bmp") == 0 ||
                      strcasecmp(ext, "rgb565") == 0 || strcasecmp(ext, "raw") == 0 ||
                      strcasecmp(ext, "bin") == 0) {
                    zmask |= (1U << z);
                    if (found_z < 0) {
                      found_z = z;
                      found_x = xval;
                      found_y = atoi(ystem);
                      strncpy(found_ext, ext, sizeof(found_ext) - 1);
                    }
                    yentry.close();
                    break;
                  }
                }
              }
            }
          }
          yentry.close();
          ++checked_y;
        }
      }
      xentry.close();
      if (zmask & (1U << z)) break;
    }
    zdir.close();
  }

  if (zmask == 0 || found_z < 0) return false;

  if (is_slash_root) {
    _tile_root[0] = '\0';
  } else {
    strncpy(_tile_root, root_dir, sizeof(_tile_root) - 1);
    _tile_root[sizeof(_tile_root) - 1] = '\0';
  }
  strncpy(_tile_ext, found_ext[0] ? found_ext : "png", sizeof(_tile_ext) - 1);
  _avail_zoom_mask = zmask;

  // Prefer Switzerland center / major Swiss hubs if present in the discovered tile set
  struct ChRef { double lat; double lon; };
  static const ChRef kSwissRefs[6] = {
    {46.8182, 8.2275},  // Geographic center of Switzerland (Obwalden)
    {47.0502, 8.3093},  // Lucerne
    {46.9480, 7.4474},  // Bern
    {47.3769, 8.5417},  // Zurich
    {46.0037, 8.9511},  // Lugano / Ticino
    {46.2044, 6.1432},  // Geneva
  };
  for (size_t i = 0; i < 6; ++i) {
    int tx = 0, ty = 0, px = 0, py = 0;
    core::geomath::latLonToTilePixel(kSwissRefs[i].lat, kSwissRefs[i].lon, found_z, tx, ty, px, py);
    char cpath[112] = {0};
    snprintf(cpath, sizeof(cpath), "%s/%d/%d/%d.%s", _tile_root, found_z, tx, ty, _tile_ext);
    if (SD.exists(cpath)) {
      found_x = tx;
      found_y = ty;
      break;
    }
  }

  _default_tile_z = found_z;
  _default_tile_x = found_x;
  _default_tile_y = found_y;
  return true;
#else
  (void)root_dir;
  return false;
#endif
}

void SdCard::discoverMapTiles() {
#ifdef ARDUINO
  if (!_mounted) return;
  _avail_zoom_mask = 0;
  _default_tile_z = -1;
  _default_tile_x = -1;
  _default_tile_y = -1;

  // Log root directory contents for diagnostics
  File rdir = SD.open("/");
  if (rdir && rdir.isDirectory()) {
    int rcount = 0;
    while (rcount < 24) {
      File rent = rdir.openNextFile();
      if (!rent) break;
      Serial.printf("[FARO-SD] Root entry: %s (dir=%d)\n", rent.name(), int(rent.isDirectory()));
      rent.close();
      ++rcount;
    }
    rdir.close();
  }

  static const char* kCandidates[] = {
    "/faro/maps",
    "/tiles",
    "/maps",
    "/map",
    "/Tiles",
    "/Maps",
    "/Map",
    "/osm",
    "/"
  };
  for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
    if (probeTileRoot(kCandidates[i])) return;
  }

  // Also scan 1-level subdirectories inside /faro/maps, /tiles, /maps, /map, and /
  static const char* kParents[] = {"/faro/maps", "/tiles", "/maps", "/map", "/"};
  for (size_t p = 0; p < sizeof(kParents) / sizeof(kParents[0]); ++p) {
    if (strcmp(kParents[p], "/") != 0 && !SD.exists(kParents[p])) continue;
    File dir = SD.open(kParents[p]);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    int scanned = 0;
    while (scanned < 16) {
      File sub = dir.openNextFile();
      if (!sub) break;
      if (sub.isDirectory()) {
        const char* bname = baseNameOf(sub.name());
        if (bname[0] != '.' &&
            strcasecmp(bname, "System Volume Information") != 0 &&
            strcasecmp(bname, "logs") != 0 &&
            strcasecmp(bname, "screenshots") != 0) {
          char subpath[64] = {0};
          if (strcmp(kParents[p], "/") == 0) {
            snprintf(subpath, sizeof(subpath), "/%s", bname);
          } else {
            snprintf(subpath, sizeof(subpath), "%s/%s", kParents[p], bname);
          }
          sub.close();
          if (probeTileRoot(subpath)) {
            dir.close();
            return;
          }
          ++scanned;
          continue;
        }
      }
      sub.close();
      ++scanned;
    }
    dir.close();
  }
#endif
}

bool SdCard::ensureFaroDirs() {
#ifdef ARDUINO
  if (!_mounted) return false;
#if defined(LILYGO_TDECK)
  digitalWrite(9, HIGH);
  digitalWrite(12, HIGH);
#endif
  if (!SD.exists("/faro")) SD.mkdir("/faro");
  if (!SD.exists("/faro/logs")) SD.mkdir("/faro/logs");
  if (!SD.exists("/faro/maps")) SD.mkdir("/faro/maps");
  if (!SD.exists("/faro/screenshots")) SD.mkdir("/faro/screenshots");
  return true;
#else
  return false;
#endif
}

bool SdCard::beginStdio() {
#ifdef ARDUINO
  if (!_mounted && !begin()) return false;
#if defined(LILYGO_TDECK)
  // LoRa (CS 9) and TFT (CS 12) share the bus: keep them deselected
  digitalWrite(9, HIGH);
  digitalWrite(12, HIGH);
#endif
  return ensureFaroDirs();
#else
  return false;
#endif
}

void SdCard::endStdio() {
#ifdef ARDUINO
  if (!_mounted) return;
  _used_mb = uint32_t(SD.usedBytes() / (1024ULL * 1024ULL));
#if defined(LILYGO_TDECK)
  digitalWrite(kPinCs, HIGH);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
#endif
#endif
}

bool SdCard::writeFile(const char* path, const uint8_t* data, size_t len) {
#ifdef ARDUINO
  if (!path || !data) return false;
  if (!_mounted && !begin()) return false;
#if defined(LILYGO_TDECK)
  digitalWrite(9, HIGH);
  digitalWrite(12, HIGH);
#endif
  ensureFaroDirs();
  if (SD.exists(path)) {
    SD.remove(path);
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    // Card may have been re-inserted or shared SPI clock disturbed: re-mount once and retry
    if (begin()) {
#if defined(LILYGO_TDECK)
      digitalWrite(9, HIGH);
      digitalWrite(12, HIGH);
#endif
      ensureFaroDirs();
      if (SD.exists(path)) SD.remove(path);
      f = SD.open(path, FILE_WRITE);
    }
  }
  if (!f) {
#if defined(LILYGO_TDECK)
    digitalWrite(kPinCs, HIGH);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    SPI.endTransaction();
#endif
    return false;
  }
  size_t written = f.write(data, len);
  f.flush();
  f.close();
  _used_mb = uint32_t(SD.usedBytes() / (1024ULL * 1024ULL));
#if defined(LILYGO_TDECK)
  digitalWrite(kPinCs, HIGH);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
#endif
  return written == len;
#else
  (void)path; (void)data; (void)len;
  return false;
#endif
}

bool SdCard::appendLine(const char* path, const char* line) {
#ifdef ARDUINO
  if (!path || !line) return false;
  if (!_mounted && !begin()) return false;
#if defined(LILYGO_TDECK)
  digitalWrite(9, HIGH);
  digitalWrite(12, HIGH);
#endif
  ensureFaroDirs();
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
#if defined(LILYGO_TDECK)
    digitalWrite(kPinCs, HIGH);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    SPI.endTransaction();
#endif
    return false;
  }
  f.println(line);
  f.close();
#if defined(LILYGO_TDECK)
  digitalWrite(kPinCs, HIGH);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
#endif
  return true;
#else
  (void)path; (void)line;
  return false;
#endif
}

int32_t SdCard::readFile(const char* path, uint8_t* buf, size_t max_len) {
#ifdef ARDUINO
  if (!path || !buf || max_len == 0) return -1;
  if (!_mounted && !begin()) return -1;
#if defined(LILYGO_TDECK)
  digitalWrite(9, HIGH);
  digitalWrite(12, HIGH);
#endif
  if (!SD.exists(path)) {
#if defined(LILYGO_TDECK)
    digitalWrite(kPinCs, HIGH);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    SPI.endTransaction();
#endif
    return -1;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
#if defined(LILYGO_TDECK)
    digitalWrite(kPinCs, HIGH);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    SPI.endTransaction();
#endif
    return -1;
  }
  size_t n = f.read(buf, max_len);
  f.close();
#if defined(LILYGO_TDECK)
  digitalWrite(kPinCs, HIGH);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
#endif
  return int32_t(n);
#else
  (void)path; (void)buf; (void)max_len;
  return -1;
#endif
}

int32_t SdCard::readTileFile(int z, int x, int y, uint8_t* buf, size_t max_len,
                             char* out_ext, size_t ext_max) {
#ifdef ARDUINO
  if (!buf || max_len == 0 || z < 0 || z > 18 || x < 0 || y < 0) return -1;
  if (!_mounted && !begin()) return -1;

  char path[112] = {0};
  // 1. Fast path: use discovered _tile_root and _tile_ext
  if (_avail_zoom_mask != 0) {
    if ((_avail_zoom_mask & (1U << z)) == 0) {
      return -1;
    }
    snprintf(path, sizeof(path), "%s/%d/%d/%d.%s", _tile_root, z, x, y, _tile_ext);
    int32_t n = readFile(path, buf, max_len);
    if (n > 0) {
      if (out_ext && ext_max > 0) {
        strncpy(out_ext, _tile_ext, ext_max - 1);
        out_ext[ext_max - 1] = '\0';
      }
      return n;
    }
  }

  // 2. Fallback across common roots and extensions
  static const char* kRoots[] = {"/faro/maps", "/tiles", "/maps", "/map", ""};
  static const char* kExts[]  = {"png", "bmp", "rgb565", "bin"};
  for (size_t r = 0; r < sizeof(kRoots) / sizeof(kRoots[0]); ++r) {
    for (size_t e = 0; e < sizeof(kExts) / sizeof(kExts[0]); ++e) {
      snprintf(path, sizeof(path), "%s/%d/%d/%d.%s", kRoots[r], z, x, y, kExts[e]);
      int32_t n = readFile(path, buf, max_len);
      if (n > 0) {
        strncpy(_tile_root, kRoots[r], sizeof(_tile_root) - 1);
        strncpy(_tile_ext, kExts[e], sizeof(_tile_ext) - 1);
        _avail_zoom_mask |= (1U << z);
        if (out_ext && ext_max > 0) {
          strncpy(out_ext, kExts[e], ext_max - 1);
          out_ext[ext_max - 1] = '\0';
        }
        return n;
      }
    }
  }
  return -1;
#else
  (void)z; (void)x; (void)y; (void)buf; (void)max_len; (void)out_ext; (void)ext_max;
  return -1;
#endif
}

}  // namespace faro::hal
