// SPDX-License-Identifier: Apache-2.0
// SdCard.h — MicroSD card helper on T-Deck Plus shared SPI bus (CS=39) (ADR-007, ADR-009)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace faro::hal {

class SdCard {
public:
  static constexpr int kPinCs   = 39;
  static constexpr int kPinSclk = 40;
  static constexpr int kPinMiso = 38;
  static constexpr int kPinMosi = 41;

  bool begin();
  bool isMounted() const { return _mounted; }
  uint32_t totalMB() const { return _total_mb; }
  uint32_t usedMB() const { return _used_mb; }

  bool ensureFaroDirs();
  void discoverMapTiles();
  bool writeFile(const char* path, const uint8_t* data, size_t len);
  bool appendLine(const char* path, const char* line);
  int32_t readFile(const char* path, uint8_t* buf, size_t max_len);
  int32_t readTileFile(int z, int x, int y, uint8_t* buf, size_t max_len,
                       char* out_ext = nullptr, size_t ext_max = 0);

  const char* tileRoot() const { return _tile_root; }
  const char* tileExt() const { return _tile_ext; }
  uint32_t availableZoomMask() const { return _avail_zoom_mask; }
  bool hasDiscoveredTiles() const { return _avail_zoom_mask != 0; }
  int defaultTileZ() const { return _default_tile_z; }
  int defaultTileX() const { return _default_tile_x; }
  int defaultTileY() const { return _default_tile_y; }

private:
  bool probeTileRoot(const char* root_dir);

  bool _mounted = false;
  uint32_t _total_mb = 0;
  uint32_t _used_mb = 0;
  char _tile_root[64] = "/faro/maps";
  char _tile_ext[12] = "png";
  uint32_t _avail_zoom_mask = 0;
  int _default_tile_z = -1;
  int _default_tile_x = -1;
  int _default_tile_y = -1;
};

}  // namespace faro::hal
