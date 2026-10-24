#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# scripts/package_release.sh — package Faro firmware and desktop builds into one release directory.
#
# Usage:  scripts/package_release.sh <version>        (or FARO_VERSION=<version> scripts/package_release.sh)
#
# Inputs (build them first; nothing is silently skipped, a missing input is an error):
#   firmware   ${FARO_PIO_BUILD:-external/MeshCore/.pio/build}/<env>/{firmware.bin,bootloader.bin,partitions.bin}
#              for every env in FARO_FW_ENVS (default: the five community envs below; set to "" to skip)
#   desktop    ${FARO_HOST_BUILD:-build}/<bin> for every bin in FARO_HOST_BINS (default: faro-sim faro-console;
#              set to "" to skip)
# Output: ${FARO_DIST_DIR:-dist}/ (wiped first) holding exactly the release assets:
#   faro-<board>-<v>-merged.bin   full image, flash at 0x0 (web flasher)
#   faro-<board>-<v>-app.bin      app only, flash at 0x10000 (update, keeps keys and contacts)
#   faro-<v>-firmware.tar.gz      all images + flash helper
#   faro-<v>-linux-<arch>.tar.gz  desktop binaries + helper scripts
#   faro-desktop_<v>_<debarch>.deb
#   LICENSE NOTICE THIRD_PARTY.md manifest.json SHA256SUMS
# Every archive and the .deb contain LICENSE, NOTICE, THIRD_PARTY.md and LICENSES/.
# SHA256SUMS is generated last, from the files actually in dist/.
# The output is then checked with scripts/check-edition.py (private markers fail the packaging).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-${FARO_VERSION:-}}"
[[ -n "${VERSION}" ]] || { echo "usage: $0 <version>   (or set FARO_VERSION)" >&2; exit 2; }
[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]] || { echo "ERROR: bad version '${VERSION}'" >&2; exit 2; }

PIO_BUILD="${FARO_PIO_BUILD:-${ROOT_DIR}/external/MeshCore/.pio/build}"
HOST_BUILD="${FARO_HOST_BUILD:-${ROOT_DIR}/build}"
DIST="${FARO_DIST_DIR:-${ROOT_DIR}/dist}"
FW_ENVS="${FARO_FW_ENVS-faro_tdeck faro_tdeck_max faro_heltec_v3 faro_tbeam_supreme faro_thinknode_m9}"
HOST_BINS="${FARO_HOST_BINS-faro-sim faro-console}"
DEB_MAINTAINER="${FARO_DEB_MAINTAINER:-Sixi AI <https://github.com/radek-sixi/sixi-tdeck-plus/issues>}"
GIT_SHA="$(git -C "${ROOT_DIR}" rev-parse --short=10 HEAD)"
MESHCORE_SHA="$(git -C "${ROOT_DIR}/external/MeshCore" rev-parse --short=10 HEAD)"

die() { echo "ERROR: $*" >&2; exit 1; }
need() { [[ -f "$1" ]] || die "missing build input: $1"; }

for f in LICENSE NOTICE docs/THIRD_PARTY.md; do need "${ROOT_DIR}/${f}"; done
[[ -d "${ROOT_DIR}/LICENSES" ]] || die "missing LICENSES/ directory"

rm -rf "${DIST}"
mkdir -p "${DIST}"
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

# copy LICENSE, NOTICE, THIRD_PARTY.md and LICENSES/ into a staging dir
add_legal() {
  local dest="$1"
  mkdir -p "${dest}"
  cp "${ROOT_DIR}/LICENSE" "${ROOT_DIR}/NOTICE" "${dest}/"
  cp "${ROOT_DIR}/docs/THIRD_PARTY.md" "${dest}/THIRD_PARTY.md"
  cp -r "${ROOT_DIR}/LICENSES" "${dest}/LICENSES"
}

# reproducible-ish tarball: sorted names, fixed owner, mtime from the commit
COMMIT_TIME="$(git -C "${ROOT_DIR}" log -1 --format=%ct)"
make_tgz() {
  local out="$1" dir="$2"
  tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@${COMMIT_TIME}" \
      -C "$(dirname "${dir}")" -cf - "$(basename "${dir}")" | gzip -n -9 > "${out}"
}

# ---------------------------------------------------------------- firmware
BOOT_APP0_DEFAULT="${HOME}/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
FW_DIR="${STAGE}/faro-${VERSION}-firmware"
FW_IMAGES=()
for ENV_NAME in ${FW_ENVS}; do
  [[ "${ENV_NAME}" == *sixi* ]] && die "env ${ENV_NAME} is not a community env"
  B="${PIO_BUILD}/${ENV_NAME}"
  BOARD="${ENV_NAME#faro_}"
  BOARD="faro-${BOARD//_/-}"
  need "${B}/firmware.bin"
  cp "${B}/firmware.bin" "${DIST}/${BOARD}-${VERSION}-app.bin"
  if [[ -f "${B}/firmware-merged.bin" ]]; then
    cp "${B}/firmware-merged.bin" "${DIST}/${BOARD}-${VERSION}-merged.bin"
  else
    need "${B}/bootloader.bin"
    need "${B}/partitions.bin"
    BOOT_APP0="${B}/boot_app0.bin"
    [[ -f "${BOOT_APP0}" ]] || BOOT_APP0="${BOOT_APP0_DEFAULT}"
    need "${BOOT_APP0}"
    python3 - "${B}" "${BOOT_APP0}" "${DIST}/${BOARD}-${VERSION}-merged.bin" <<'PY'
import pathlib, sys
build_dir, boot_app0, out_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
app = (build_dir / "firmware.bin").read_bytes()
img = bytearray(b"\xFF" * (0x10000 + len(app)))
for off, part in ((0x0000, build_dir / "bootloader.bin"), (0x8000, build_dir / "partitions.bin"),
                  (0xE000, boot_app0), (0x10000, build_dir / "firmware.bin")):
    data = part.read_bytes()
    img[off:off + len(data)] = data
out_path.write_bytes(bytes(img))
PY
  fi
  FW_IMAGES+=("${BOARD}-${VERSION}-merged.bin" "${BOARD}-${VERSION}-app.bin")
done

if [[ ${#FW_IMAGES[@]} -gt 0 ]]; then
  add_legal "${FW_DIR}"
  for f in "${FW_IMAGES[@]}"; do cp "${DIST}/${f}" "${FW_DIR}/"; done
  need "${ROOT_DIR}/scripts/flash_easy.sh"
  install -m 0755 "${ROOT_DIR}/scripts/flash_easy.sh" "${FW_DIR}/flash_easy.sh"
  [[ -f "${ROOT_DIR}/docs/FLASHING.md" ]] && cp "${ROOT_DIR}/docs/FLASHING.md" "${FW_DIR}/"
  make_tgz "${DIST}/faro-${VERSION}-firmware.tar.gz" "${FW_DIR}"
fi

# ---------------------------------------------------------------- desktop
if [[ -n "${HOST_BINS// /}" ]]; then
  case "$(uname -m)" in
    x86_64)  ARCH="x86_64";  DEBARCH="amd64" ;;
    aarch64) ARCH="aarch64"; DEBARCH="arm64" ;;
    *) die "unsupported host architecture $(uname -m)" ;;
  esac
  for BIN in ${HOST_BINS}; do
    [[ "${BIN}" == *sixi* ]] && die "${BIN} is not a community binary"
    need "${HOST_BUILD}/${BIN}"
  done

  LNX_DIR="${STAGE}/faro-${VERSION}-linux-${ARCH}"
  add_legal "${LNX_DIR}"
  for BIN in ${HOST_BINS}; do install -m 0755 "${HOST_BUILD}/${BIN}" "${LNX_DIR}/${BIN}"; done
  for SCRIPT_FILE in run_pc_app.sh tbeam_wifi_pc.py make_sd_map.py; do
    need "${ROOT_DIR}/scripts/${SCRIPT_FILE}"
    install -m 0755 "${ROOT_DIR}/scripts/${SCRIPT_FILE}" "${LNX_DIR}/${SCRIPT_FILE}"
  done
  [[ -f "${ROOT_DIR}/docs/QUICKSTART_PL.md" ]] && cp "${ROOT_DIR}/docs/QUICKSTART_PL.md" "${LNX_DIR}/"
  make_tgz "${DIST}/faro-${VERSION}-linux-${ARCH}.tar.gz" "${LNX_DIR}"

  PKG_DIR="${STAGE}/faro-desktop_${VERSION}_${DEBARCH}"
  DOC_DIR="${PKG_DIR}/usr/share/doc/faro-desktop"
  mkdir -p "${PKG_DIR}/DEBIAN" "${PKG_DIR}/usr/bin"
  for BIN in ${HOST_BINS}; do install -m 0755 "${HOST_BUILD}/${BIN}" "${PKG_DIR}/usr/bin/${BIN}"; done
  add_legal "${DOC_DIR}"
  cat > "${DOC_DIR}/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Faro
Source: https://github.com/radek-sixi/sixi-tdeck-plus

Files: *
Copyright: 2026 Sixi AI
License: Apache-2.0
 On Debian systems the full text is in /usr/share/common-licenses/Apache-2.0
 and in /usr/share/doc/faro-desktop/LICENSE.

Files: third-party components compiled into faro-sim and faro-console
Copyright: see /usr/share/doc/faro-desktop/NOTICE
License: MIT and OFL-1.1 and Zlib and Bitstream-Vera and public-domain
 LVGL 9.3 (MIT) with LodePNG (Zlib), QR Code generator (MIT), Montserrat and
 Font Awesome symbols (OFL-1.1), UNSCII (public domain); DejaVu-derived fonts
 (Bitstream Vera). Full texts: /usr/share/doc/faro-desktop/LICENSES/.
 Details: /usr/share/doc/faro-desktop/THIRD_PARTY.md.
EOF
  cat > "${PKG_DIR}/DEBIAN/control" <<EOF
Package: faro-desktop
Version: ${VERSION}
Section: comm
Priority: optional
Architecture: ${DEBARCH}
Depends: libsdl2-2.0-0, libpng16-16t64 | libpng16-16
Maintainer: ${DEB_MAINTAINER}
Homepage: https://github.com/radek-sixi/sixi-tdeck-plus
Description: Faro desktop console and simulator for MeshCore
 Native LVGL 9 desktop companion console (faro-console) and UI simulator
 (faro-sim) for MeshCore companion radios over WiFi TCP (:5000) or USB serial.
EOF
  dpkg-deb --root-owner-group --build "${PKG_DIR}" "${DIST}/faro-desktop_${VERSION}_${DEBARCH}.deb"
fi

# ---------------------------------------------------------------- top-level assets
cp "${ROOT_DIR}/LICENSE" "${ROOT_DIR}/NOTICE" "${DIST}/"
cp "${ROOT_DIR}/docs/THIRD_PARTY.md" "${DIST}/THIRD_PARTY.md"

python3 - "${DIST}" "${VERSION}" "${GIT_SHA}" "${MESHCORE_SHA}" <<'PY'
import json, pathlib, sys
dist, version, faro_sha, meshcore_sha = pathlib.Path(sys.argv[1]), *sys.argv[2:]
files = sorted(p.name for p in dist.iterdir() if p.is_file())
manifest = {
    "product": "faro",
    "version": version,
    "faro_commit": faro_sha,
    "meshcore_commit": meshcore_sha,
    "firmware": {p[len("faro-"):-len(f"-{version}-merged.bin")]: {"merged_0x0": p, "app_0x10000": p.replace("-merged.bin", "-app.bin")}
                 for p in files if p.endswith(f"-{version}-merged.bin")},
    "files": files,
}
(dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
PY

(
  cd "${DIST}"
  rm -f SHA256SUMS
  SUMS_TMP="$(mktemp)"
  find . -maxdepth 1 -type f -printf '%f\n' | LC_ALL=C sort | xargs -d '\n' sha256sum > "${SUMS_TMP}"
  mv "${SUMS_TMP}" SHA256SUMS
  chmod 0644 SHA256SUMS
  sha256sum --quiet -c SHA256SUMS
)

python3 "${ROOT_DIR}/scripts/check-edition.py" --no-history "${DIST}"

echo "[package_release] Faro ${VERSION} (faro ${GIT_SHA}, MeshCore ${MESHCORE_SHA}) -> ${DIST}"
ls -1 "${DIST}"
