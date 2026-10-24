#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
scripts/make_sd_map.py — Offline SD map tile downloader for Faro
Writes /faro/maps/{z}/{x}/{y}.png slippy-map tiles around a GPS coordinate,
ready to copy onto a FAT32 microSD card.

There is NO default tile server. You must name one with --tiles-url, and you must
use a provider whose terms allow offline / bulk download (for example a commercial
plan that permits it, or tiles you render yourself from OpenStreetMap data with
your own tile server). tile.openstreetmap.org forbids bulk and offline downloads
(https://operations.osmfoundation.org/policies/tiles/), so the script refuses it.

Tiles made from OpenStreetMap data must carry "© OpenStreetMap contributors"
(ODbL 1.0, https://www.openstreetmap.org/copyright) plus whatever your provider requires.

Usage:
  ./scripts/make_sd_map.py --tiles-url 'http://localhost:8080/tile/{z}/{x}/{y}.png' \
      --attribution '© OpenStreetMap contributors' \
      --lat 46.0207 --lon 7.7491 --zooms 11,12,13 --radius 1 --out ./sd_card
  ./scripts/make_sd_map.py --offline-only --lat 46.0207 --lon 7.7491 --out ./sd_card   # placeholders, no network
"""
import argparse
import math
import pathlib
import sys
import time
import urllib.parse
import urllib.request

USER_AGENT = "FaroSdMapTool/0.9 (+https://github.com/radek-sixi/sixi-tdeck-plus; offline SD tiles for Faro)"
OSM_ATTRIBUTION = "© OpenStreetMap contributors"
# Hosts whose usage policy forbids bulk/offline downloads. Add to this list, never remove.
FORBIDDEN_HOSTS = ("tile.openstreetmap.org", "tile.osm.org")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def deg2num(lat_deg: float, lon_deg: float, zoom: int) -> tuple[int, int]:
    lat_rad = math.radians(lat_deg)
    n = 2.0 ** zoom
    xtile = int((lon_deg + 180.0) / 360.0 * n)
    ytile = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return xtile, ytile


def check_tiles_url(template: str) -> None:
    for key in ("{z}", "{x}", "{y}"):
        if key not in template:
            raise SystemExit(f"ERROR: --tiles-url must contain {{z}}, {{x}} and {{y}} (missing {key})")
    parts = urllib.parse.urlsplit(template.replace("{z}", "0").replace("{x}", "0").replace("{y}", "0"))
    if parts.scheme not in ("http", "https", "file"):
        raise SystemExit("ERROR: --tiles-url must be an http(s):// or file:// URL")
    host = (parts.hostname or "").lower()
    if any(host == h or host.endswith("." + h) for h in FORBIDDEN_HOSTS):
        raise SystemExit(
            f"ERROR: {host} does not allow bulk or offline tile downloads "
            "(https://operations.osmfoundation.org/policies/tiles/).\n"
            "Use a provider whose terms allow offline use, or render your own tiles.")


def plan_tiles(lat: float, lon: float, zooms: list[int], radius: int) -> list[tuple[int, int, int]]:
    tiles = []
    for z in zooms:
        n = 2 ** z
        cx, cy = deg2num(lat, lon, z)
        for dx in range(-radius, radius + 1):
            for dy in range(-radius, radius + 1):
                tx, ty = (cx + dx) % n, cy + dy
                if 0 <= ty < n:
                    tiles.append((z, tx, ty))
    return tiles


def main() -> int:
    parser = argparse.ArgumentParser(description="Faro offline SD map tile generator")
    parser.add_argument("--tiles-url", type=str, default=None,
                        help="Tile URL template with {z}/{x}/{y}, from a provider that allows offline download "
                             "(required unless --offline-only)")
    parser.add_argument("--attribution", type=str, default=OSM_ATTRIBUTION,
                        help=f"Attribution text the tiles require (default: '{OSM_ATTRIBUTION}')")
    parser.add_argument("--lat", type=float, required=True, help="Center latitude")
    parser.add_argument("--lon", type=float, required=True, help="Center longitude")
    parser.add_argument("--zooms", type=str, default="11,12,13", help="Comma-separated zoom levels (max 17)")
    parser.add_argument("--radius", type=int, default=1, help="Tile radius around center (1 => 3x3 tiles per zoom)")
    parser.add_argument("--max-tiles", type=int, default=500, help="Refuse to fetch more than this many tiles")
    parser.add_argument("--delay", type=float, default=1.0, help="Seconds to wait between requests (min 0.5)")
    parser.add_argument("--out", type=str, default="./sd_faro_maps", help="Output root directory")
    parser.add_argument("--offline-only", action="store_true", help="Write placeholder PNG headers without network")
    args = parser.parse_args()

    zooms = sorted({int(z.strip()) for z in args.zooms.split(",") if z.strip()})
    if not zooms or min(zooms) < 0 or max(zooms) > 17:
        raise SystemExit("ERROR: --zooms must be between 0 and 17")
    if args.radius < 0 or args.radius > 20:
        raise SystemExit("ERROR: --radius must be between 0 and 20")
    if not args.offline_only:
        if not args.tiles_url:
            parser.error("--tiles-url is required (there is no default tile server); or use --offline-only")
        check_tiles_url(args.tiles_url)
    delay = max(0.5, args.delay)

    tiles = plan_tiles(args.lat, args.lon, zooms, args.radius)
    if len(tiles) > args.max_tiles:
        raise SystemExit(f"ERROR: {len(tiles)} tiles requested, limit is {args.max_tiles} (--max-tiles). "
                         "Lower --radius/--zooms or check your provider's limits first.")

    out_root = pathlib.Path(args.out)
    maps_root = out_root / "faro" / "maps"
    written, skipped, failed = 0, 0, 0
    for i, (z, tx, ty) in enumerate(tiles):
        tile_path = maps_root / str(z) / str(tx) / f"{ty}.png"
        tile_path.parent.mkdir(parents=True, exist_ok=True)
        if args.offline_only:
            tile_path.write_bytes(PNG_MAGIC)
            written += 1
            continue
        if tile_path.exists() and tile_path.stat().st_size > len(PNG_MAGIC):
            skipped += 1  # never re-download what we already have
            continue
        url = args.tiles_url.replace("{z}", str(z)).replace("{x}", str(tx)).replace("{y}", str(ty))
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = resp.read()
            if not data.startswith(PNG_MAGIC):
                raise ValueError("response is not a PNG")
            tile_path.write_bytes(data)
            written += 1
            print(f"[OK] {tile_path}")
        except Exception as exc:  # keep going; report at the end
            failed += 1
            print(f"[FAIL] {url} ({exc})", file=sys.stderr)
        if i + 1 < len(tiles):
            time.sleep(delay)

    print(f"\nDone: {written} written, {skipped} already present, {failed} failed, in {maps_root}.")
    print("Copy the 'faro' folder to the root of your FAT32 microSD card (/faro/maps/...).")
    attribution_file = maps_root / "ATTRIBUTION.txt"
    if not args.offline_only:
        maps_root.mkdir(parents=True, exist_ok=True)
        attribution_file.write_text(f"{args.attribution}\nTiles: {urllib.parse.urlsplit(args.tiles_url).netloc}\n",
                                    encoding="utf-8")
        print(f"\nATTRIBUTION REQUIRED: {args.attribution}")
        print(f"Written to {attribution_file}. Keep it with the tiles and show it wherever the map is shown;")
        print("also follow any extra attribution or licence terms of your tile provider.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
