#!/usr/bin/env bash
# link_env.sh - attach our PlatformIO envs to the unmodified MeshCore submodule (ADR-002).
#
# Creates external/MeshCore/platformio.local.ini -> ../../envs/faro.ini
# Upstream's platformio.ini loads platformio.local.ini via extra_configs and git-ignores it,
# so the submodule stays clean. Idempotent. Refuses to overwrite a regular file.
#
# Usage: scripts/link_env.sh [repo-root]
set -euo pipefail
ROOT="$(cd "${1:-.}" && pwd)"
SUB="$ROOT/external/MeshCore"
TARGET_REL="../../envs/faro.ini"
LINK="$SUB/platformio.local.ini"

[ -e "$SUB/platformio.ini" ] || { echo "FAIL: $SUB not initialised (git submodule update --init)"; exit 1; }
[ -f "$ROOT/envs/faro.ini" ] || { echo "FAIL: $ROOT/envs/faro.ini missing"; exit 1; }
grep -q "platformio.local.ini" "$SUB/platformio.ini" || { echo "FAIL: upstream no longer loads platformio.local.ini - needs a new ADR"; exit 1; }

if [ -L "$LINK" ]; then
  current="$(readlink "$LINK")"
  if [ "$current" = "$TARGET_REL" ]; then echo "OK: link already in place"; exit 0; fi
  rm "$LINK"
elif [ -e "$LINK" ]; then
  echo "FAIL: $LINK is a regular file - move it away (it could hide local changes)"; exit 1
fi

ln -s "$TARGET_REL" "$LINK"
echo "OK: $LINK -> $TARGET_REL"
git -C "$SUB" status --porcelain --untracked-files=all | grep -q . && { echo "WARN: submodule shows changes - check .gitignore"; exit 1; } || true
