# Flashing Faro

Release files (GitHub release `v0.11.5`):

| File | Flash at | Use |
|---|---|---|
| `faro-<board>-0.11.5-merged.bin` | `0x0` | First install or recovery: bootloader, partition table, `boot_app0`, app |
| `faro-<board>-0.11.5-app.bin` | `0x10000` | Update of an existing Faro or MeshCore companion install; keeps identity, contacts, channels |
| `SHA256SUMS` | — | Check first: `sha256sum -c SHA256SUMS` |

`<board>` is `tdeck` (T-Deck and T-Deck Plus), `tdeck-max`, `thinknode-m9`, `tbeam-supreme` or `heltec-v3`.

0.10.1 was the first release with the radio regions and the data Faro keeps on flash (both below). It also
fixed a T-Deck Plus bootloop in the battery ADC that affected 0.10.0; if you still run 0.10.0, update.
0.10.2 lowered idle power draw (screen off after 5 min idle, lower default brightness, Wi-Fi modem powered
down when disabled). 0.10.3 added Wi-Fi network scanning, I2S audio notifications, touch map drag/pan,
emoji support, preset dropdowns, high-contrast textbox focus/cursor, and MicroSD backup fixes.
0.10.4 fixed ESP32-S3 Wi-Fi scan driver parameters, BLE MITM pairing & bond recovery, screen idle
dimming/sleep, graphical emoji glyphs and Settings tab navigation. 0.10.6 fixed Wi-Fi configured at
runtime, restored network scanning (Arduino fallback), kept the mandated modem sleep for BLE+Wi-Fi,
decoded offline map PNG tiles from PSRAM with a 9-tile cache, and removed the community
lone-worker/dead-man timer. 0.10.7 added a Reconnect action when Wi-Fi is already connected and a
Show/Hide toggle for the password being typed. 0.10.8 added a hidden Snake easter egg: type `snake`
while no text field is focused. 0.11.0 adds message management (delete/clear/search a thread and jump
to the first unread), a configurable CPU profile and an About/Support tab. 0.11.1 adds phone Wi-Fi
setup (temporary WPA2 access point `Faro-XXXX` with a small web form, auto-stops after 5 min), a
**2048** easter egg (type `2048` outside a text field) and a north arrow + 40 px scale bar on the map.
0.11.2 splits `App.cpp` into themed translation units with shared helpers in `AppUiShared.h` — no
functional change. 0.11.3 declutters the map: at most the 8 contact pins closest to the map centre are
drawn (the rest collapse into a "+N more" badge) and the GPS/SD status moved off the canvas into the
bottom HUD button. 0.11.4 fixes the 2048 board not repainting after a move (LVGL 9.3 coalesces
style-only changes; `update2048` now forces a full-screen invalidation) and adds a pixel-level
regression test for it. 0.11.5 adds a letter alias for the 2048 easter egg: type `tile` (the T-Deck
digit layer is awkward; `2048` still works where a number row exists).

## Web flasher (browser, no command line)

You can flash from Chrome or Edge without installing anything, using an
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) flasher. Put the device in download mode
(off → hold the trackball → on → release; the screen stays dark), connect USB-C, then in the flasher pick
**ESP32-S3** and flash the **merged** image at `0x0` (first install) or the **app** image at `0x10000` (update).

- **[espflasher.app](https://espflasher.app/)** — load the downloaded `-merged.bin` file, or paste a URL.
- Browser flashing by URL needs `Access-Control-Allow-Origin`. The release assets on `github.com` redirect to a
  host without it, so for URL-based flashing use the CORS-enabled copy on GitHub Pages:

  ```
  https://radek-sixi.github.io/sixi-tdeck-plus/firmware/v0.11.5/faro-tdeck-0.11.5-merged.bin
  ```

  (same pattern for the other boards and the `-app.bin`; the Mesh America configurator catalog also lists Faro).

## 0. Back up the device first

```bash
pip install esptool
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 read-flash 0 ALL backup-$(date +%F).bin
```

Writing that file back at `0x0` restores the device exactly. The backup contains the node's private key:
keep it somewhere safe.

## 1. Download mode (T-Deck)

Switch the device off, connect USB-C, hold the trackball pressed, switch on, release. The screen stays dark.

## 2. Flash

```bash
# first install
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0x0 faro-tdeck-0.11.5-merged.bin
# update, keeps data
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0x10000 faro-tdeck-0.11.5-app.bin
```

On Windows the port is `COMx`, on macOS `/dev/cu.usbmodem*`. If 921600 baud fails, use 460800.

From source: `scripts/link_env.sh && pio run -d external/MeshCore -e faro_tdeck -t upload`.

## Identity and settings

Faro uses the upstream MeshCore partition table (`default_16MB.csv` on the T-Deck: app at `0x10000`,
SPIFFS at `0xc90000`) and the upstream DataStore. Updating from a stock MeshCore companion keeps the node's
identity, contacts and channels. Firmware with its own storage (for example Ripple) does not share it, so
Faro then starts with a new identity.

To move an identity between devices: Settings › SD › *Export Keys -> SD* (tap twice to confirm) writes
`/faro/identity.bak`; *Import Keys <- SD* on the other device reads it and restarts. The file is not
encrypted; delete it from the card afterwards.

## Radio settings after the first boot

A new device starts on the preset *EU/UK/CH Narrow*: 869.618 MHz, 62.5 kHz, SF8, CR 4/8, 22 dBm. The welcome screen
shown at the first start of this Faro version lets you pick the region of your country (Settings › Radio later).
Settings stored by an earlier firmware are kept when they fit one of Faro's regions (the region follows them);
otherwise Faro switches to the region's first preset and shows the new values in Settings › Radio. Frequency,
bandwidth, SF and CR have to match on every node of a mesh; TX power is per device.

The regions and their limits are listed in the README (*Radio regions*). Use the hardware variant for your band
(e.g. a 433 MHz board for EU 433): the radio chip tunes outside its antenna's band, but it will barely work.

## Data Faro keeps

Next to MeshCore's own files, Faro writes `/faro_ui.bin` (region, UI settings, contact tags/notes) and
`/faro_msgs.bin` (chat history) to the same SPIFFS partition. A firmware update at `0x10000` keeps them; a full
flash at `0x0` or a factory reset deletes them. `clear history` (Settings › CLI) deletes only the chat history.

## Rollback

Download mode, then `write-flash 0x0 backup-<date>.bin`, or flash any MeshCore firmware from
<https://flasher.meshcore.io>.
