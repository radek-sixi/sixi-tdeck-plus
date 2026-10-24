# Links and sources

## Upstream MeshCore (MIT) — source of truth
| What | URL |
|---|---|
| Firmware repo (use tag `companion-v1.18.0` or newer) | https://github.com/meshcore-dev/MeshCore |
| Docs site | https://docs.meshcore.io/ |
| Companion protocol (BLE only documented; TCP/USB framing: read `src/helpers/wifi/SerialWifiInterface.cpp`, `src/helpers/ArduinoSerialInterface.cpp`) | https://docs.meshcore.io/companion_protocol/ |
| Packet / payload formats | https://docs.meshcore.io/packet_format/ · https://docs.meshcore.io/payloads/ |
| QR code formats (`meshcore://contact/add`, `meshcore://channel/add`) | https://docs.meshcore.io/qr_codes/ |
| Number allocations (GRP_DATA types; dev range FF00–FFFF) | https://docs.meshcore.io/number_allocations/ |
| CLI commands (repeater / room / sensor admin) | https://docs.meshcore.io/cli_commands/ |
| Stats binary frames | https://docs.meshcore.io/stats_binary_frames/ |
| FAQ (T-Deck DFU, GPS baud 38400, map tiles, flashing) | https://docs.meshcore.io/faq/ |
| Radio presets (served by app API) | https://docs.meshcore.io/radio_presets/ · https://api.meshcore.nz/api/v1/config |
| PR #3591 — WiFi on Heltec V3/V4 BLE companions (model for upstream-PR 001) | https://github.com/meshcore-dev/MeshCore/pull/3591 |
| Contributing rules (branch from `dev`) | `reference/meshcore-docs-1.18.0/CONTRIBUTING-MeshCore.md` |
| Web flasher (stock firmwares, Ripple) | https://flasher.meshcore.io |
| Python library `meshcore` (MIT) — serial / BLE / TCP client | https://github.com/meshcore-dev/meshcore_py · `pip install meshcore` |
| MeshCore web client (MIT, Liam Cottle) — UI structure reference | https://github.com/liamcottle/meshcore-web |
| MeshCore JS library (MIT) | https://github.com/liamcottle/meshcore.js |
| Community project list | https://github.com/samuk/awesome-meshcore |

## Hardware
| What | URL |
|---|---|
| LilyGO T-Deck official repo (pins: `examples/UnitTest/utilities.h`, keyboard firmware) | https://github.com/Xinyuan-LilyGO/T-Deck |
| LilyGO T-Deck Plus product page | https://lilygo.cc/products/t-deck-plus-1 |
| Map tile downloader (OSM, `/tiles/{z}/{x}/{y}.png`) | https://github.com/fistulareffigy/MTD-Script · https://github.com/TheBestJohn/MTD-Script |

## Libraries and tools (check licences into `docs/THIRD_PARTY.md`)
| What | URL | Licence |
|---|---|---|
| LVGL 9 | https://github.com/lvgl/lvgl · https://docs.lvgl.io/ | MIT |
| lv_font_conv | https://github.com/lvgl/lv_font_conv | MIT |
| Adafruit ST7735/ST7789 (used by upstream) | https://github.com/adafruit/Adafruit-ST7735-Library | MIT |
| RadioLib (status codes, e.g. -8 invalid bandwidth) | https://jgromes.github.io/RadioLib/group__status__codes.html | MIT |
| PlatformIO | https://docs.platformio.org/ | Apache-2.0 |
| esptool | https://github.com/espressif/esptool | GPL-2.0 (tool only, not linked) |
| Espressif web flasher | https://espressif.github.io/esptool-js/ | — |
| Inter font | https://github.com/rsms/inter | OFL-1.1 |
| Noto Sans | https://github.com/notofonts/latin-greek-cyrillic | OFL-1.1 |
| DejaVu Fonts (source of `app/ui/fonts/faro_font_*.c`) | https://dejavu-fonts.github.io/License.html | Bitstream Vera + Arev |
| ESP Web Tools (web flasher, pinned 10.4.0 + SRI) | https://github.com/esphome/esp-web-tools | Apache-2.0 |

## Map data and tiles
| What | URL |
|---|---|
| OpenStreetMap copyright and attribution ("© OpenStreetMap contributors", ODbL 1.0) | https://www.openstreetmap.org/copyright |
| OSMF Tile Usage Policy (no bulk or offline downloads from tile.openstreetmap.org) | https://operations.osmfoundation.org/policies/tiles/ |
| Tile providers and self-hosting options (pick one whose terms allow offline use for `scripts/make_sd_map.py --tiles-url`) | https://wiki.openstreetmap.org/wiki/Raster_tile_providers · https://switch2osm.org/ |

## Functional references (closed products — features only, never copy assets or code)
| What | Where |
|---|---|
| Ripple blog: v9.7 themes and accent colour | https://blog.meshcore.io/2026/04/30/ripple-9.7 |
| Ripple guide online | https://buymeacoffee.com/ripplebiz/ultra-v7-7-guide-meshcore-users |
| MeshOS feature list | https://meshcore.co.uk/meshos.html |
| MeshCore phone app changelog (summary in `reference/meshcore-app-features.md`) | https://app.meshcore.nz/assets/CHANGELOG.md |

## Do NOT open the source of (GPL — clean-room rule, ADR-004)
- wadamesh (ALLFATHER-BV): READMEs and release notes are OK for feature ideas
- SigurdOS-tdeck: README OK
- Meshtastic device-ui / firmware UI code
