// SPDX-License-Identifier: Apache-2.0
// app/core/GeoMath.h — Great-circle distance, bearing, and Web Mercator slippy-tile math
#pragma once

#include <math.h>
#include <stdint.h>

namespace faro::core::geomath {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kEarthRadiusMeters = 6371000.0;

inline double degToRad(double deg) {
  return deg * (kPi / 180.0);
}

inline double radToDeg(double rad) {
  return rad * (180.0 / kPi);
}

// Haversine great-circle distance in meters between two WGS84 points.
inline double distanceMeters(double lat1_deg, double lon1_deg,
                             double lat2_deg, double lon2_deg) {
  const double dlat = degToRad(lat2_deg - lat1_deg);
  const double dlon = degToRad(lon2_deg - lon1_deg);
  const double rlat1 = degToRad(lat1_deg);
  const double rlat2 = degToRad(lat2_deg);

  const double sin_dlat = sin(dlat * 0.5);
  const double sin_dlon = sin(dlon * 0.5);
  const double a = sin_dlat * sin_dlat +
                   cos(rlat1) * cos(rlat2) * sin_dlon * sin_dlon;
  const double c = 2.0 * atan2(sqrt(a), sqrt(fmax(0.0, 1.0 - a)));
  return kEarthRadiusMeters * c;
}

// Initial forward azimuth in degrees [0, 360) from (lat1, lon1) to (lat2, lon2).
inline double bearingDegrees(double lat1_deg, double lon1_deg,
                             double lat2_deg, double lon2_deg) {
  const double rlat1 = degToRad(lat1_deg);
  const double rlat2 = degToRad(lat2_deg);
  const double dlon  = degToRad(lon2_deg - lon1_deg);

  const double y = sin(dlon) * cos(rlat2);
  const double x = cos(rlat1) * sin(rlat2) -
                   sin(rlat1) * cos(rlat2) * cos(dlon);
  double brng = radToDeg(atan2(y, x));
  brng = fmod(brng + 360.0, 360.0);
  if (brng < 0.0) brng += 360.0;
  return brng;
}

// 8-wind cardinal compass direction for a bearing in [0, 360).
inline const char* cardinalDirection(double bearing_deg) {
  static constexpr const char* kDirs[8] = {
    "N", "NE", "E", "SE", "S", "SW", "W", "NW"
  };
  double b = fmod(bearing_deg + 360.0, 360.0);
  int idx = int(floor((b + 22.5) / 45.0)) & 7;
  return kDirs[idx];
}

// Web Mercator slippy-tile fractional coordinates at zoom level z (0..18).
inline void latLonToTileDouble(double lat_deg, double lon_deg, int z,
                               double& out_tx, double& out_ty) {
  if (lat_deg > 85.05112878) lat_deg = 85.05112878;
  if (lat_deg < -85.05112878) lat_deg = -85.05112878;
  const double n = double(1U << (z < 0 ? 0 : (z > 18 ? 18 : z)));
  out_tx = ((lon_deg + 180.0) / 360.0) * n;
  const double lat_rad = degToRad(lat_deg);
  out_ty = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / kPi) * 0.5 * n;
}

// Integer slippy-tile indices (x, y) and sub-tile pixel offsets (px, py in 0..255).
inline void latLonToTilePixel(double lat_deg, double lon_deg, int z,
                              int& tile_x, int& tile_y,
                              int& pixel_x, int& pixel_y) {
  double tx = 0.0, ty = 0.0;
  latLonToTileDouble(lat_deg, lon_deg, z, tx, ty);
  const int max_tile = (1 << (z < 0 ? 0 : (z > 18 ? 18 : z))) - 1;
  tile_x = int(floor(tx));
  tile_y = int(floor(ty));
  pixel_x = int(floor((tx - double(tile_x)) * 256.0));
  pixel_y = int(floor((ty - double(tile_y)) * 256.0));
  if (tile_x < 0) tile_x = 0;
  if (tile_x > max_tile) tile_x = max_tile;
  if (tile_y < 0) tile_y = 0;
  if (tile_y > max_tile) tile_y = max_tile;
  if (pixel_x < 0) pixel_x = 0;
  if (pixel_x > 255) pixel_x = 255;
  if (pixel_y < 0) pixel_y = 0;
  if (pixel_y > 255) pixel_y = 255;
}

// Converts slippy-tile (z, tile_x, tile_y) center to WGS84 (lat_deg, lon_deg).
inline void tileCenterToLatLon(int z, int tile_x, int tile_y,
                               double& out_lat_deg, double& out_lon_deg) {
  const int zz = (z < 0 ? 0 : (z > 18 ? 18 : z));
  const double n = double(1U << zz);
  out_lon_deg = ((double(tile_x) + 0.5) / n) * 360.0 - 180.0;
  const double lat_rad = atan(sinh(kPi * (1.0 - 2.0 * (double(tile_y) + 0.5) / n)));
  out_lat_deg = radToDeg(lat_rad);
}

// Official Swisstopo WGS84 -> SwissGrid CH1903+ / LV95 (Easting E ~ 2'600'000, Northing N ~ 1'200'000)
inline void wgs84ToSwissLV95(double lat_deg, double lon_deg,
                             int32_t& out_east_m, int32_t& out_north_m) {
  const double phi = (lat_deg * 3600.0 - 169028.66) / 10000.0;
  const double lam = (lon_deg * 3600.0 - 26782.50) / 10000.0;
  const double e = 2600072.37
                 + 211455.93 * lam
                 - 10938.51 * lam * phi
                 - 0.36 * lam * phi * phi
                 - 44.54 * lam * lam * lam;
  const double n = 1200147.07
                 + 308807.95 * phi
                 + 3745.25 * lam * lam
                 + 76.63 * phi * phi
                 - 194.56 * lam * lam * phi
                 + 119.79 * phi * phi * phi;
  out_east_m  = int32_t(llround(e));
  out_north_m = int32_t(llround(n));
}

// WGS84 -> UTM (Zone 1..60, Band C..X, Easting m, Northing m)
inline void wgs84ToUtm(double lat_deg, double lon_deg,
                       int& out_zone, char& out_band,
                       int32_t& out_easting_m, int32_t& out_northing_m) {
  if (lat_deg > 84.0) lat_deg = 84.0;
  if (lat_deg < -80.0) lat_deg = -80.0;
  out_zone = int(floor((lon_deg + 180.0) / 6.0)) + 1;
  if (out_zone < 1) out_zone = 1;
  if (out_zone > 60) out_zone = 60;

  static constexpr const char* kBands = "CDEFGHJKLMNPQRSTUVWXX";
  int b_idx = int(floor((lat_deg + 80.0) / 8.0));
  if (b_idx < 0) b_idx = 0;
  if (b_idx > 20) b_idx = 20;
  out_band = kBands[b_idx];

  constexpr double a = 6378137.0;
  constexpr double eccSq = 0.00669437999014;
  constexpr double k0 = 0.9996;
  const double latRad = degToRad(lat_deg);
  const double lonRad = degToRad(lon_deg);
  const double lonOriginRad = degToRad(double((out_zone - 1) * 6 - 180 + 3));
  const double eccPrimeSq = eccSq / (1.0 - eccSq);

  const double N = a / sqrt(1.0 - eccSq * sin(latRad) * sin(latRad));
  const double T = tan(latRad) * tan(latRad);
  const double C = eccPrimeSq * cos(latRad) * cos(latRad);
  const double A = cos(latRad) * (lonRad - lonOriginRad);
  const double M = a * ((1.0 - eccSq / 4.0 - 3.0 * eccSq * eccSq / 64.0) * latRad
                      - (3.0 * eccSq / 8.0 + 3.0 * eccSq * eccSq / 32.0) * sin(2.0 * latRad)
                      + (15.0 * eccSq * eccSq / 256.0) * sin(4.0 * latRad));

  const double easting = k0 * N * (A + (1.0 - T + C) * A * A * A / 6.0) + 500000.0;
  double northing = k0 * (M + N * tan(latRad) * (A * A / 2.0 + (5.0 - T + 9.0 * C + 4.0 * C * C) * A * A * A * A / 24.0));
  if (lat_deg < 0.0) northing += 10000000.0;

  out_easting_m  = int32_t(llround(easting));
  out_northing_m = int32_t(llround(northing));
}

// Semtech SX1262 LoRa Time-on-Air (ms) calculation (AN1200.13)
inline float loraTimeOnAirMs(uint16_t payload_bytes, uint8_t sf, float bw_khz, uint8_t cr) {
  if (sf < 5) sf = 5;
  if (sf > 12) sf = 12;
  if (bw_khz < 7.8f) bw_khz = 62.5f;
  uint8_t cr_denom = (cr >= 5 && cr <= 8) ? uint8_t(cr - 4) : 1;

  const float t_sym_ms = float(1U << sf) / bw_khz;
  const float t_preamble_ms = (8.0f + 4.25f) * t_sym_ms;
  const int de = (t_sym_ms >= 16.0f) ? 1 : 0;  // Low Data Rate Optimization
  const int ih = 0;                            // Explicit header
  const int crc = 1;                           // CRC on

  const float num = float(8 * int(payload_bytes) - 4 * int(sf) + 28 + 16 * crc - 20 * ih);
  const float den = float(4 * (int(sf) - 2 * de));
  float ceil_term = ceilf(num / den) * float(cr_denom + 4);
  if (ceil_term < 0.0f) ceil_term = 0.0f;
  const float n_payload = 8.0f + ceil_term;
  return t_preamble_ms + n_payload * t_sym_ms;
}

// Semtech SX1262 receiver sensitivity in dBm for given SF and BW (kHz)
inline float sx1262RxSensitivityDbm(uint8_t sf, float bw_khz) {
  if (bw_khz < 7.8f) bw_khz = 62.5f;
  // Required SNR (dB) per SF5..SF12
  static constexpr float kSnrReq[13] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    -2.5f, -5.0f, -7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f
  };
  uint8_t s = (sf >= 5 && sf <= 12) ? sf : 8;
  const float thermal_noise = -174.0f + 10.0f * log10f(bw_khz * 1000.0f);
  const float nf_db = 6.0f;
  return thermal_noise + nf_db + kSnrReq[s];
}

// Total LoRa Link Budget in dB (TX power dBm - RX sensitivity dBm)
inline float loraLinkBudgetDb(int8_t tx_power_dbm, uint8_t sf, float bw_khz) {
  return float(tx_power_dbm) - sx1262RxSensitivityDbm(sf, bw_khz);
}

// Max transmissions per hour within 10% ETSI/BAKOM duty cycle (360,000 ms/hour)
inline uint32_t maxPacketsPerHour10Pct(float toa_ms) {
  if (toa_ms <= 0.1f) return 99999U;
  return uint32_t(360000.0f / toa_ms);
}

}  // namespace faro::core::geomath
