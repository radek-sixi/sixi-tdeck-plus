#!/usr/bin/env bash
# verify_upstream_clean.sh - enforce the "zero patches in MeshCore" rule (ADR-002).
#
# Fails when:
#   1. the MeshCore submodule is not checked out at a companion-v* or sixi-v* tag
#   2. the submodule has ANY tracked modification or untracked file
#      (git-ignored files such as .pio/ and platformio.local.ini are allowed)
#   3. platformio.local.ini exists but is not a symlink into our repo (a real file
#      there could hide build-time patches)
#   4. our app/ tree contains copies of upstream companion sources (forking by copy)
#
# Usage: scripts/verify_upstream_clean.sh [path/to/MeshCore] [path/to/app]
set -euo pipefail

SUB="${1:-external/MeshCore}"
APP="${2:-app}"
PREFIX="${EXPECTED_TAG_PREFIX:-companion-v}"
fail() { echo "FAIL: $*" >&2; exit 1; }

[ -e "$SUB/.git" ] || fail "$SUB is not a git checkout/submodule"

tag="$(git -C "$SUB" describe --exact-match --tags HEAD 2>/dev/null || true)"
[ -n "$tag" ] || fail "$SUB HEAD ($(git -C "$SUB" rev-parse --short HEAD)) is not exactly on a tag"
case "$tag" in
  "$PREFIX"*|sixi-v*) ;;
  *) fail "$SUB is on tag '$tag', expected a ${PREFIX}* or sixi-v* tag" ;;
esac

dirty="$(git -C "$SUB" status --porcelain --untracked-files=all)"
[ -z "$dirty" ] || fail "$SUB has local changes (zero-patch rule):
$dirty"

if [ -e "$SUB/platformio.local.ini" ] && [ ! -L "$SUB/platformio.local.ini" ]; then
  fail "$SUB/platformio.local.ini must be a symlink to our envs file, not a regular file"
fi

if [ -d "$APP" ]; then
  copies="$(find "$APP" -type f \( -name 'MyMesh.cpp' -o -name 'MyMesh.h' -o -name 'main.cpp' \
            -o -name 'DataStore.cpp' -o -name 'BaseChatMesh.*' -o -name 'Mesh.cpp' -o -name 'Dispatcher.*' \) || true)"
  [ -z "$copies" ] || fail "upstream source files copied into $APP (use the submodule instead):
$copies"
fi

echo "OK: $SUB is clean at $tag ($(git -C "$SUB" rev-parse --short HEAD))"
