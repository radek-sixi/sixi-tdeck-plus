#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# scripts/build_firmware.sh — build firmware envs, each in its own PlatformIO build dir.
#
# PlatformIO places the objects of sources outside the project (../../app) next to the build dir,
# not inside the env's directory, so envs built into one build dir overwrite each other's app/
# objects. Every env therefore gets its own build dir here.
#
# Usage: scripts/build_firmware.sh OUT_DIR [env ...]   (default: the five public envs)
# Result: OUT_DIR/<env>/{firmware.bin,bootloader.bin,partitions.bin,firmware.elf}
#         (the layout scripts/package_release.sh reads with FARO_PIO_BUILD=OUT_DIR)
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: $0 OUT_DIR [env ...]}"
shift || true
ENVS=("$@")
[[ ${#ENVS[@]} -gt 0 ]] || ENVS=(faro_tdeck faro_tdeck_max faro_heltec_v3 faro_tbeam_supreme faro_thinknode_m9)
mkdir -p "${OUT}"
OUT="$(cd "${OUT}" && pwd)"
for env in "${ENVS[@]}"; do
  work="${OUT}/.work/${env}/b"
  PLATFORMIO_BUILD_DIR="${work}" pio run -d "${ROOT_DIR}/external/MeshCore" -e "${env}"
  mkdir -p "${OUT}/${env}"
  for f in firmware.bin bootloader.bin partitions.bin firmware.elf; do
    cp "${work}/${env}/${f}" "${OUT}/${env}/${f}"
  done
done
echo "build_firmware: ${#ENVS[@]} env(s) in ${OUT}"
