# Faro — an on-device UI for MeshCore

Faro is a touch, keyboard and trackball UI (LVGL 9.3) for the [MeshCore](https://github.com/meshcore-dev/MeshCore)
companion firmware. It runs on the device next to the normal companion interfaces, so the MeshCore phone app
(BLE) and TCP clients (Wi-Fi, port 5000) keep working while you use the screen.

- **MeshCore:** unmodified upstream `companion-v1.18.0` (`81dbae5`), pinned as the `external/MeshCore` submodule.
  Faro attaches through upstream's `platformio.local.ini` hook and the `AbstractUITask` listener; it adds no
  companion opcodes.
- **Licence:** Apache-2.0 (see `LICENSE`, `NOTICE`, `LICENSES/`, `docs/THIRD_PARTY.md`).
- **Status:** 0.9.2, pre-1.0. Test it on your own hardware before relying on it.

## Boards

| Board | Env | Screen UI |
|---|---|---|
| LilyGo T-Deck / T-Deck Plus | `faro_tdeck` | yes |
| LilyGo T-Deck MAX | `faro_tdeck_max` | yes |
| Elecrow ThinkNode M9 | `faro_thinknode_m9` | yes |
| LilyGo T-Beam S3 Supreme | `faro_tbeam_supreme` | headless (BLE + Wi-Fi companion) |
| Heltec WiFi LoRa 32 V3 | `faro_heltec_v3` | headless (BLE + Wi-Fi companion) |

Also in this repository: a desktop console (`faro-console`, SDL2) that talks to a node over USB or TCP, and a
headless simulator (`faro-sim`) that renders every screen to PNG.

## What it does

Chats and channels (including `#hashtag` channels), contact discovery and QR/URI sharing, trace path,
offline map tiles from the SD card, repeater roaming, radio and node settings, diagnostics (packet sniffer,
link budget, mesh graph), GPX tracks, SD backup of settings, and EN/IT/DE/FR text for the main screens.

## Radio limits

Faro only offers settings inside **869.400–869.650 MHz** (the band used by the MeshCore EU presets), checked for
the whole channel (centre ± bandwidth/2):

- transmit power is capped so that e.r.p. stays at or below **27 dBm (500 mW)**, assuming a 3 dBi antenna
  (`FARO_ANTENNA_GAIN_DBI`; e.r.p. = conducted + gain − 2.15 dB); the radio's own limit is 22 dBm;
- the duty cycle is limited to **10 %** through MeshCore's airtime factor (at least 9).

At boot, stored settings outside these limits (for example from another firmware) are brought inside them;
the change is shown in Settings › Radio and printed on the serial console. Other regions are not offered in
this release. You are responsible for operating within the rules of your country.

## Security notes

- The Wi-Fi companion port (TCP 5000) is upstream MeshCore's and has **no authentication**: anyone on the same
  network can use it like the phone app. Only enable Wi-Fi on networks you trust.
- Private-key import/export over the companion protocol is **off** (upstream default). A key backup to the SD
  card is possible from Settings › SD after a confirmation tap; the file is not encrypted, so remove it from the
  card when done.
- New private channels get a random key from the hardware RNG. `Public` and `#name` channels have well-known keys.
- BLE PIN: random on screen-equipped boards; weak PINs (123456, repeated or sequential digits) are refused.
- Message text is never written to the serial log.

Report vulnerabilities as described in `SECURITY.md`.

## Building

```sh
git clone --recurse-submodules https://github.com/radek-sixi/sixi-tdeck-plus.git
cd sixi-tdeck-plus
scripts/link_env.sh                                  # links envs/faro.ini into the MeshCore submodule
pio run -d external/MeshCore -e faro_tdeck           # firmware
cmake -S . -B build && cmake --build build -j && ctest --test-dir build   # host tests, sim, console
```

`docs/FLASHING.md` explains how to flash and how to keep your existing MeshCore identity.

## Maps

`scripts/make_sd_map.py` prepares tiles for the SD card from a tile server **you** choose; it refuses the
OpenStreetMap tile servers, whose usage policy forbids bulk downloads. Map data © OpenStreetMap contributors
where OSM data is used.
