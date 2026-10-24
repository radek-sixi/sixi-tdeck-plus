// SPDX-License-Identifier: Apache-2.0
// app/core/OfflineTileReader.cpp — Slippy-tile reader (/faro/maps/{z}/{x}/{y}) with LRU cache & procedural fallback
#include "OfflineTileReader.h"
#include <stdint.h>
#include "GeoMath.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace faro::core {

namespace {
uint16_t* allocTilePixels() {
#ifdef ARDUINO
  if (psramFound()) {
    void* p = ps_malloc(256U * 256U * sizeof(uint16_t));
    if (p) return static_cast<uint16_t*>(p);
  }
#endif
  return static_cast<uint16_t*>(malloc(256U * 256U * sizeof(uint16_t)));
}

void freeTilePixels(uint16_t* p) {
  if (p) free(p);
}

uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((uint16_t(r) & 0xF8U) << 8) |
                  ((uint16_t(g) & 0xFCU) << 3) |
                  (uint16_t(b) >> 3));
}

uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

int32_t readLeI32(const uint8_t* p) {
  return static_cast<int32_t>(readLe32(p));
}

uint16_t readLe16(const uint8_t* p) {
  return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8);
}
}  // namespace

OfflineTileReader::~OfflineTileReader() {
  for (size_t i = 0; i < kCacheSlots; ++i) {
    freeTilePixels(_cache[i].pixels);
    _cache[i].pixels = nullptr;
  }
}

void OfflineTileReader::formatTilePath(int z, int x, int y, char* out_path, size_t max_len,
                                       const char* ext) {
  if (!out_path || max_len == 0) return;
  if (z < 0 || z > 18 || x < 0 || y < 0) {
    out_path[0] = '\0';
    return;
  }
  const char* safe_ext = "png";
  if (ext && (strcmp(ext, "png") == 0 || strcmp(ext, "rgb565") == 0 || strcmp(ext, "bmp") == 0)) {
    safe_ext = ext;
  }
  snprintf(out_path, max_len, "/faro/maps/%d/%d/%d.%s", z, x, y, safe_ext);
}

bool OfflineTileReader::decodeRawOrBmpToRgb565(const uint8_t* data, size_t len, uint16_t* out_256x256) {
  if (!data || !out_256x256 || len < 16) return false;

  // 1. Raw 256x256 RGB565 (131,072 bytes)
  if (len == 256U * 256U * 2U) {
    memcpy(out_256x256, data, len);
    return true;
  }

  // 2. Uncompressed BMP ("BM" header, 16-bit, 24-bit, or 32-bit)
  if (len >= 54 && data[0] == 'B' && data[1] == 'M') {
    uint32_t pixel_offset = readLe32(data + 10);
    int32_t bmp_w = readLeI32(data + 18);
    int32_t bmp_h = readLeI32(data + 22);
    uint16_t bpp = readLe16(data + 28);
    uint32_t compression = readLe32(data + 30);
    // Tiles are small; bounding the size keeps every product below in 32-bit range
    static constexpr int32_t kMaxBmpDim = 4096;
    if (bmp_h == INT32_MIN) return false;
    bool top_down = (bmp_h < 0);
    if (bmp_h < 0) bmp_h = -bmp_h;
    if (bmp_w <= 0 || bmp_h <= 0 || bmp_w > kMaxBmpDim || bmp_h > kMaxBmpDim) return false;
    if (pixel_offset >= len) return false;
    if (compression != 0 && compression != 3) return false;
    if (bpp != 16 && bpp != 24 && bpp != 32) return false;

    uint32_t row_stride = ((uint32_t(bmp_w) * bpp + 31U) / 32U) * 4U;
    for (int y = 0; y < 256; ++y) {
      int sy = (y * bmp_h) / 256;
      if (sy >= bmp_h) sy = bmp_h - 1;
      int file_row = top_down ? sy : (bmp_h - 1 - sy);
      size_t row_off = size_t(pixel_offset) + size_t(file_row) * size_t(row_stride);
      if (row_off >= len) break;
      const uint8_t* row_ptr = data + row_off;
      for (int x = 0; x < 256; ++x) {
        int sx = (x * bmp_w) / 256;
        if (sx >= bmp_w) sx = bmp_w - 1;
        size_t px_off = size_t(sx) * (bpp / 8U);
        if (row_off + px_off + (bpp / 8U) > len) continue;
        const uint8_t* p = row_ptr + px_off;
        if (bpp == 16) {
          out_256x256[y * 256 + x] = readLe16(p);
        } else {
          // BMP stores B, G, R
          out_256x256[y * 256 + x] = packRgb565(p[2], p[1], p[0]);
        }
      }
    }
    return true;
  }

  return false;
}

bool OfflineTileReader::hasTile(int z, int x, int y) {
  TileKey k{z, x, y};
  ++_access_counter;

  for (size_t i = 0; i < kCacheSlots; ++i) {
    if (_cache[i].valid && _cache[i].key == k) {
      _cache[i].last_access = _access_counter;
      ++_hits;
      return _cache[i].exists_on_disk;
    }
  }

  ++_misses;
  size_t lru_idx = 0;
  uint32_t oldest = UINT32_MAX;
  for (size_t i = 0; i < kCacheSlots; ++i) {
    if (!_cache[i].valid) {
      lru_idx = i;
      break;
    }
    if (_cache[i].last_access < oldest) {
      oldest = _cache[i].last_access;
      lru_idx = i;
    }
  }

  bool found = false;
  bool decoded = false;
  if (_fs) {
    if (!_cache[lru_idx].pixels) {
      _cache[lru_idx].pixels = allocTilePixels();
    }
    if (_cache[lru_idx].pixels && _fs->readTileRgb565(z, x, y, _cache[lru_idx].pixels)) {
      found = true;
      decoded = true;
    } else {
      char path[96] = {0};
      formatTilePath(z, x, y, path, sizeof(path), "png");
      uint8_t header[16] = {0};
      if (_fs->readTileFile(path, header, sizeof(header)) > 0) {
        found = true;
      }
    }
  }

  _cache[lru_idx].key = k;
  _cache[lru_idx].last_access = _access_counter;
  _cache[lru_idx].exists_on_disk = found;
  _cache[lru_idx].decoded_ok = decoded;
  _cache[lru_idx].valid = true;
  return found;
}

const uint16_t* OfflineTileReader::getDecodedTile(int z, int x, int y) {
  if (!_fs || z < 0 || z > 18 || x < 0 || y < 0) return nullptr;

  TileKey k{z, x, y};
  ++_access_counter;

  for (size_t i = 0; i < kCacheSlots; ++i) {
    if (_cache[i].valid && _cache[i].key == k) {
      _cache[i].last_access = _access_counter;
      ++_hits;
      if (_cache[i].decoded_ok && _cache[i].pixels) {
        return _cache[i].pixels;
      }
      if (!_cache[i].exists_on_disk) {
        return nullptr;
      }
      // Tile existed in metadata check; attempt full decode now
      if (!_cache[i].pixels) {
        _cache[i].pixels = allocTilePixels();
      }
      if (_cache[i].pixels && _fs->readTileRgb565(z, x, y, _cache[i].pixels)) {
        _cache[i].decoded_ok = true;
        return _cache[i].pixels;
      }
      return nullptr;
    }
  }

  ++_misses;
  size_t lru_idx = 0;
  uint32_t oldest = UINT32_MAX;
  for (size_t i = 0; i < kCacheSlots; ++i) {
    if (!_cache[i].valid) {
      lru_idx = i;
      break;
    }
    if (_cache[i].last_access < oldest) {
      oldest = _cache[i].last_access;
      lru_idx = i;
    }
  }

  if (!_cache[lru_idx].pixels) {
    _cache[lru_idx].pixels = allocTilePixels();
  }

  bool decoded = _cache[lru_idx].pixels && _fs->readTileRgb565(z, x, y, _cache[lru_idx].pixels);
  bool exists = decoded;
  if (!decoded) {
    char path[96] = {0};
    formatTilePath(z, x, y, path, sizeof(path), "png");
    uint8_t header[16] = {0};
    if (_fs->readTileFile(path, header, sizeof(header)) > 0) {
      exists = true;
    }
  }

  _cache[lru_idx].key = k;
  _cache[lru_idx].last_access = _access_counter;
  _cache[lru_idx].exists_on_disk = exists;
  _cache[lru_idx].decoded_ok = decoded;
  _cache[lru_idx].valid = true;
  return decoded ? _cache[lru_idx].pixels : nullptr;
}

bool OfflineTileReader::getDefaultTileCenter(double& out_lat, double& out_lon, int& out_zoom) const {
  if (!_fs) return false;
  int tz = -1, tx = -1, ty = -1;
  if (!_fs->getDefaultTileCoord(tz, tx, ty) || tz < 0) return false;
  geomath::tileCenterToLatLon(tz, tx, ty, out_lat, out_lon);
  out_zoom = tz;
  return true;
}

void OfflineTileReader::renderViewportRgb565(double center_lat, double center_lon, int z,
                                             uint16_t* dst_rgb565, int width, int height,
                                             bool outdoor_high_contrast) {
  if (!dst_rgb565 || width <= 0 || height <= 0) return;
  _last_render_used_sd_tiles = false;

  int cx_tile = 0, cy_tile = 0, cx_px = 0, cy_px = 0;
  geomath::latLonToTilePixel(center_lat, center_lon, z, cx_tile, cy_tile, cx_px, cy_px);

  const int32_t world_cx = cx_tile * 256 + cx_px;
  const int32_t world_cy = cy_tile * 256 + cy_px;
  const int32_t top_left_wx = world_cx - (width / 2);
  const int32_t top_left_wy = world_cy - (height / 2);

  // 1. Base procedural topographic/grid background (used wherever SD tiles are absent)
  const uint16_t bg_col     = outdoor_high_contrast ? 0xF7DF : 0x0862;  // #F8F9FA Google Maps Light vs dark slate
  const uint16_t sub_grid   = outdoor_high_contrast ? 0xE73D : 0x10E4;  // #E8EAED subtle 32px grid
  const uint16_t tile_grid  = outdoor_high_contrast ? 0xD6BA : 0x2188;  // #DADCE0 256px tile boundary
  const uint16_t contour    = outdoor_high_contrast ? 0xCE7A : 0x1145;  // #CEEAD6 Google Maps elevation contour

  for (int y = 0; y < height; ++y) {
    const int32_t wy = top_left_wy + y;
    uint16_t* row = dst_rgb565 + (y * width);
    for (int x = 0; x < width; ++x) {
      const int32_t wx = top_left_wx + x;
      uint16_t c = bg_col;
      if ((wx & 255) == 0 || (wy & 255) == 0) {
        c = tile_grid;
      } else if ((wx & 31) == 0 || (wy & 31) == 0) {
        c = sub_grid;
      } else if (((wx * wx + wy * wy) / 180) % 29 == 0) {
        c = contour;
      }
      row[x] = c;
    }
  }

  // 2. Composite real decoded SD tiles (with up to 4 levels of parent-tile overzoom fallback)
  const int max_tile = (1 << (z < 0 ? 0 : (z > 18 ? 18 : z))) - 1;
  const int tx_min = top_left_wx >> 8;
  const int tx_max = (top_left_wx + width - 1) >> 8;
  const int ty_min = top_left_wy >> 8;
  const int ty_max = (top_left_wy + height - 1) >> 8;

  for (int ty = ty_min; ty <= ty_max; ++ty) {
    if (ty < 0 || ty > max_tile) continue;
    for (int tx = tx_min; tx <= tx_max; ++tx) {
      if (tx < 0 || tx > max_tile) continue;

      int overzoom_dz = 0;
      const uint16_t* tile_px = getDecodedTile(z, tx, ty);
      if (!tile_px) {
        for (int dz = 1; dz <= 4 && (z - dz) >= 0; ++dz) {
          tile_px = getDecodedTile(z - dz, tx >> dz, ty >> dz);
          if (tile_px) {
            overzoom_dz = dz;
            break;
          }
        }
      }
      if (!tile_px) continue;

      _last_render_used_sd_tiles = true;

      int x0 = (tx * 256) - top_left_wx;
      if (x0 < 0) x0 = 0;
      int x1 = ((tx + 1) * 256) - top_left_wx;
      if (x1 > width) x1 = width;
      int y0 = (ty * 256) - top_left_wy;
      if (y0 < 0) y0 = 0;
      int y1 = ((ty + 1) * 256) - top_left_wy;
      if (y1 > height) y1 = height;
      if (x0 >= x1 || y0 >= y1) continue;

      if (overzoom_dz == 0) {
        for (int y = y0; y < y1; ++y) {
          const int py = (top_left_wy + y) & 255;
          const int px0 = (top_left_wx + x0) & 255;
          memcpy(dst_rgb565 + (y * width) + x0,
                 tile_px + (py * 256) + px0,
                 size_t(x1 - x0) * sizeof(uint16_t));
        }
      } else {
        const int mask = (1 << overzoom_dz) - 1;
        const int sub_x_base = (tx & mask) * 256;
        const int sub_y_base = (ty & mask) * 256;
        for (int y = y0; y < y1; ++y) {
          const int py = (sub_y_base + ((top_left_wy + y) & 255)) >> overzoom_dz;
          uint16_t* dst_row = dst_rgb565 + (y * width);
          const uint16_t* src_row = tile_px + (py * 256);
          for (int x = x0; x < x1; ++x) {
            const int px = (sub_x_base + ((top_left_wx + x) & 255)) >> overzoom_dz;
            dst_row[x] = src_row[px];
          }
        }
      }
    }
  }
}

}  // namespace faro::core
