#!/usr/bin/env bash
# license_audit.sh - keep the public repo Apache-2.0 clean (ADR-003, ADR-004).
#
# Checks:
#   1. no GPL/AGPL/LGPL license headers or SPDX ids in our own trees
#      (app tests sim console tools scripts envs templates web-flasher docs)
#   2. LICENSE is the Apache License 2.0
#   3. NOTICE credits MeshCore and LVGL; every LICENSES/ file NOTICE points to exists,
#      and every LICENSES/ file is referenced by NOTICE
#   4. docs/THIRD_PARTY.md exists
#   5. generated DejaVu fonts (app/ui/fonts/*.c) carry the Bitstream-Vera SPDX id, not Apache-2.0
#   6. every library PlatformIO compiled for a community env (external/MeshCore/.pio/build/<env>/lib*/)
#      is listed in docs/THIRD_PARTY.md (skipped when nothing is built; FARO_AUDIT_ENVS overrides the envs)
#   7. prints PlatformIO lib_deps for manual license review (recorded in docs/THIRD_PARTY.md)
#
# Usage: scripts/license_audit.sh [repo-root]
set -euo pipefail
ROOT="${1:-.}"
cd "$ROOT"
fail() { echo "FAIL: $*" >&2; exit 1; }

DIRS=()
for d in app tests sim console tools scripts envs templates web-flasher docs; do [ -d "$d" ] && DIRS+=("$d"); done
[ "${#DIRS[@]}" -gt 0 ] || fail "no source dirs found (expected app/, tests/, sim/, console/, tools/, scripts/, envs/)"

# grep exits 1 when nothing matches (the good case) and 2 on a real error
set +e
hits="$(grep -rIlE 'GNU (Affero |Lesser )?General Public License|SPDX-License-Identifier:[[:space:]]*(A?GPL|LGPL)' \
        "${DIRS[@]}" --exclude-dir=.pio --exclude=license_audit.sh)"
rc=$?
set -e
[ "$rc" -le 1 ] || fail "grep failed while scanning ${DIRS[*]}"
[ -z "$hits" ] || fail "copyleft license markers found in our sources (no GPL code allowed):
$hits"

[ -f LICENSE ] || fail "LICENSE missing"
if ! grep -q "Apache License" LICENSE || ! grep -q "Version 2.0" LICENSE; then fail "LICENSE is not Apache-2.0"; fi

[ -f NOTICE ] || fail "NOTICE missing"
grep -qi "MeshCore" NOTICE || fail "NOTICE must credit MeshCore (MIT, Scott Powell / rippleradios.com)"
grep -qi "LVGL" NOTICE || fail "NOTICE must credit LVGL (MIT)"
[ -d LICENSES ] || fail "LICENSES/ missing (full licence texts shipped with every release)"
while read -r ref; do
  [ -f "$ref" ] || fail "NOTICE points to $ref, which does not exist"
done < <(grep -oE 'LICENSES/[A-Za-z0-9._-]+' NOTICE | sort -u)
for f in LICENSES/*; do
  grep -qF "$f" NOTICE || fail "$f is not referenced by NOTICE"
done
[ -f docs/THIRD_PARTY.md ] || fail "docs/THIRD_PARTY.md missing"

for f in app/ui/fonts/*.c; do
  [ -e "$f" ] || continue
  if grep -q -- '--font .*dejavu' "$f"; then
    head -1 "$f" | grep -q 'SPDX-License-Identifier: Bitstream-Vera' \
      || fail "$f is generated from DejaVu: its SPDX id must be Bitstream-Vera (see LICENSES/DejaVu-Bitstream-Vera.txt)"
  fi
done

if [ -d app/assets/fonts ]; then
  for f in app/assets/fonts/*; do
    [ -e "$f" ] || continue
    base="$(basename "$f" | sed 's/\.[^.]*$//')"
    grep -qi "$base" NOTICE || fail "font/asset '$base' not mentioned in NOTICE"
  done
fi

# libraries actually compiled into community firmware (PlatformIO keeps one lib*/<Name>/ dir per library)
PIO_BUILD="external/MeshCore/.pio/build"
ENVS="${FARO_AUDIT_ENVS:-faro_tdeck faro_tdeck_max faro_heltec_v3 faro_tbeam_supreme faro_thinknode_m9}"
# parts of the arduino-esp32 core (documented as one component)
CORE_LIBS=" BLE FS LittleFS SD SD_MMC SPI SPIFFS WiFi WiFiClientSecure Wire Preferences Update WebServer ESPmDNS \
DNSServer HTTPClient HTTPUpdate Ticker AsyncUDP ArduinoOTA Ethernet NetBIOS FFat USB EEPROM I2S ESP32 "
missing=""
checked=0
for env in $ENVS; do
  [ -d "$PIO_BUILD/$env" ] || continue
  for d in "$PIO_BUILD/$env"/lib*/*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    case "$CORE_LIBS" in *" $name "*) continue ;; esac
    checked=$((checked + 1))
    # "Adafruit_VL53L0X" -> "Adafruit VL53L0X", "Adafruit BME280 Library" -> "Adafruit BME280"
    norm="$(printf '%s' "$name" | sed 's/_/ /g; s/ Library$//')"
    short="$(printf '%s' "$norm" | sed 's/^Adafruit //')"
    if ! grep -qiF "$name" docs/THIRD_PARTY.md && ! grep -qiF "$norm" docs/THIRD_PARTY.md \
       && ! grep -qiF "$short" docs/THIRD_PARTY.md; then
      missing="$missing
  $env: $name"
    fi
  done
done
[ -z "$missing" ] || fail "libraries compiled into community firmware but not listed in docs/THIRD_PARTY.md:$missing"
if [ "$checked" -eq 0 ]; then echo "NOTE: no community firmware build found under $PIO_BUILD - linked-library check skipped"; fi

echo "--- PlatformIO lib_deps (verify each license in docs/THIRD_PARTY.md) ---"
deps="$(sed 's/;.*$//' envs/*.ini | grep -hE '^[[:space:]]+[A-Za-z0-9_./@:~^ #-]+[[:space:]]*$' \
  | grep -E '@|https?://|symlink://' | sed 's/^[[:space:]]*/  /; s/[[:space:]]*$//' | sort -u)" \
  || fail "could not read lib_deps from envs/*.ini"
echo "$deps"
echo "OK: license audit passed"
