// SPDX-License-Identifier: Apache-2.0
// app/core/OfflineTileReader.h — Slippy-tile reader (/faro/maps/{z}/{x}/{y}) with LRU cache & procedural fallback
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace faro::core {

class ITileFileSystem {
public:
  virtual ~ITileFileSystem() = default;
  virtual int32_t readTileFile(const char* path, uint8_t* buf, size_t max_len) = 0;

  // Optional enriched tile reader: reads & decodes tile (z, x, y) into a 256x256 RGB565 buffer.
  virtual bool readTileRgb565(int z, int x, int y, uint16_t* out_256x256) {
    (void)z; (void)x; (void)y; (void)out_256x256;
    return false;
  }

  virtual uint32_t availableZoomMask() const { return 0; }
  virtual bool getDefaultTileCoord(int& out_z, int& out_x, int& out_y) const {
    (void)out_z; (void)out_x; (void)out_y;
    return false;
  }
};

struct TileKey {
  int z = 0;
  int x = 0;
  int y = 0;
  constexpr TileKey() = default;
  constexpr TileKey(int z_, int x_, int y_) : z(z_), x(x_), y(y_) {}
  bool operator==(const TileKey& o) const {
    return z == o.z && x == o.x && y == o.y;
  }
};

class OfflineTileReader {
public:
  static constexpr size_t kCacheSlots = 4;

  explicit OfflineTileReader(ITileFileSystem* fs = nullptr) : _fs(fs) {}
  ~OfflineTileReader();

  OfflineTileReader(const OfflineTileReader&) = delete;
  OfflineTileReader& operator=(const OfflineTileReader&) = delete;

  void setFileSystem(ITileFileSystem* fs) { _fs = fs; }

  // Formats /faro/maps/{z}/{x}/{y}.png into out_path
  static void formatTilePath(int z, int x, int y, char* out_path, size_t max_len,
                             const char* ext = "png");

  // Decodes raw 256x256 RGB565 (131072B) or uncompressed BMP (16/24/32-bit) into out_256x256
  static bool decodeRawOrBmpToRgb565(const uint8_t* data, size_t len, uint16_t* out_256x256);

  // Queries whether tile (z, x, y) is available on SD/filesystem (uses 4-slot LRU cache)
  bool hasTile(int z, int x, int y);

  // Returns decoded 256x256 RGB565 tile from 4-slot LRU cache or decodes from filesystem
  const uint16_t* getDecodedTile(int z, int x, int y);

  // Renders a 16-bit RGB565 map viewport of width x height centered on (center_lat, center_lon) at zoom z.
  // Renders real SD tiles (with parent overzoom fallback) when present, or procedural fallback when absent.
  void renderViewportRgb565(double center_lat, double center_lon, int z,
                            uint16_t* dst_rgb565, int width, int height,
                            bool outdoor_high_contrast = false);

  bool lastRenderUsedSdTiles() const { return _last_render_used_sd_tiles; }
  uint32_t availableZoomMask() const { return _fs ? _fs->availableZoomMask() : 0; }
  bool getDefaultTileCenter(double& out_lat, double& out_lon, int& out_zoom) const;

  uint32_t cacheHits() const { return _hits; }
  uint32_t cacheMisses() const { return _misses; }

private:
  struct CacheEntry {
    TileKey   key = {-1, -1, -1};
    uint32_t  last_access = 0;
    bool      exists_on_disk = false;
    bool      decoded_ok = false;
    bool      valid = false;
    uint16_t* pixels = nullptr;
  };

  ITileFileSystem* _fs = nullptr;
  CacheEntry _cache[kCacheSlots] = {};
  uint32_t _access_counter = 0;
  uint32_t _hits = 0;
  uint32_t _misses = 0;
  bool _last_render_used_sd_tiles = false;
};

}  // namespace faro::core
