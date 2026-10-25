# Flashing Faro

Release files (GitHub release `v0.9.2`):

| File | Flash at | Use |
|---|---|---|
| `faro-<board>-0.9.2-merged.bin` | `0x0` | First install or recovery: bootloader, partition table, `boot_app0`, app |
| `faro-<board>-0.9.2-app.bin` | `0x10000` | Update of an existing Faro or MeshCore companion install; keeps identity, contacts, channels |
| `SHA256SUMS` | — | Check first: `sha256sum -c SHA256SUMS` |

`<board>` is `tdeck` (T-Deck and T-Deck Plus), `tdeck-max`, `thinknode-m9`, `tbeam-supreme` or `heltec-v3`.

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
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0x0 faro-tdeck-0.9.2-merged.bin
# update, keeps data
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0x10000 faro-tdeck-0.9.2-app.bin
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

A new device starts on the preset *EU/CH Narrow*: 869.618 MHz, 62.5 kHz, SF8, CR 4/8, 22 dBm. Settings stored
by an earlier firmware are kept; to change to the preset, pick it in Settings › Radio. Frequency, bandwidth, SF
and CR have to match on every node of a mesh; TX power is per device.

If the stored radio settings are outside the band Faro allows (869.400–869.650 MHz, ≤ 27 dBm e.r.p.,
≤ 10 % duty cycle), Faro adjusts them at boot and shows the new values in Settings › Radio.

## Rollback

Download mode, then `write-flash 0x0 backup-<date>.bin`, or flash any MeshCore firmware from
<https://flasher.meshcore.io>.
