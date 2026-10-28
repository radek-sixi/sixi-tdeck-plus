#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
scripts/build_mesh_america_catalog.py — build the Mesh America Device Configurator catalog.

Mesh America's configurator reads one static JSON file and fetches firmware over HTTPS with CORS
(https://apps.meshamerica.com/validate). GitHub Release assets redirect to a host without
Access-Control-Allow-Origin, so this script publishes the files from GitHub Pages instead:

  _site/catalog.json                      stable URL, the one sent to Mesh America
  _site/firmware/<tag>/<asset>.bin        immutable per release
  _site/index.html                        tiny landing page (same content as the catalog)

It reads the newest published releases (drafts and prereleases are skipped), downloads the merged and
app binaries for every board with an on-device UI and writes them into the output directory. Device
names are matched character-for-character against the official MeshCore catalog, so the firmware is
folded into the existing device tiles instead of creating duplicates.

Usage:
  scripts/build_mesh_america_catalog.py --out _site --base-url https://radek-sixi.github.io/sixi-tdeck-plus

Requires the `gh` CLI (GH_TOKEN in CI). Exit status: 0 ok, 1 download/build error, 2 usage error.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Boards with a screen. The headless builds (T-Beam Supreme, Heltec V3) are deliberately left out:
# on those boards Faro keeps upstream's behaviour and adds nothing on-device.
DEVICES = (
    {
        "board": "tdeck",
        "name": "LilyGo T-Deck",
        "subTitle": "on-device UI",
    },
    {
        "board": "tdeck-max",
        "name": "LilyGo T-Deck Max",
        "subTitle": "on-device UI",
    },
    {
        "board": "thinknode-m9",
        "name": "Elecrow ThinkNode M9",
        "subTitle": "keypad UI",
    },
)

DESCRIPTION = (
    "Faro adds an on-device touch, keyboard and trackball UI to MeshCore companion firmware. "
    "It runs on unmodified MeshCore, so the phone app over Bluetooth and TCP clients over Wi-Fi "
    "keep working."
)

DEFAULT_REPO = "radek-sixi/sixi-tdeck-plus"


def run(cmd, **kwargs):
    print("+", " ".join(str(c) for c in cmd), file=sys.stderr)
    return subprocess.run(cmd, text=True, **kwargs)


def release_list(repo, limit):
    out = run(
        [
            "gh", "release", "list",
            "--repo", repo,
            "--limit", str(limit),
            "--exclude-drafts",
            "--json", "tagName,name,isPrerelease,publishedAt",
        ],
        check=True,
        capture_output=True,
    )
    releases = json.loads(out.stdout)
    return [r for r in releases if not r["isPrerelease"]]


def download(repo, tag, dest):
    patterns = []
    for pattern in ("faro-*-merged.bin", "faro-*-app.bin"):
        patterns += ["--pattern", pattern]
    result = run(
        ["gh", "release", "download", tag, "--repo", repo, "--dir", str(dest), "--clobber", *patterns],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        print(f"warning: could not download {tag}: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def version_files(tag, ver, board, assets, out_dir, base_url):
    files = []
    for kind, suffix, title in (
        ("flash-wipe", "merged", "Full install (bootloader + firmware)"),
        ("flash-update", "app", "Update (app only)"),
    ):
        name = f"faro-{board}-{ver}-{suffix}.bin"
        if name not in assets:
            continue
        target_dir = out_dir / "firmware" / tag
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(assets[name], target_dir / name)
        files.append(
            {
                "type": kind,
                "name": name,
                "url": f"{base_url}/firmware/{tag}/{name}",
                "title": title,
            }
        )
    return files


def build_catalog(releases, repo, out_dir, base_url, work_dir):
    versions = {device["board"]: {} for device in DEVICES}

    for release in releases:
        tag = release["tagName"]
        ver = tag.lstrip("v")
        tmp = work_dir / tag
        tmp.mkdir(parents=True, exist_ok=True)
        if not download(repo, tag, tmp):
            continue
        assets = {p.name: p for p in tmp.iterdir() if p.is_file()}

        for device in DEVICES:
            files = version_files(tag, ver, device["board"], assets, out_dir, base_url)
            if not files:
                print(f"warning: {tag}: no files for {device['board']}", file=sys.stderr)
                continue
            versions[device["board"]][ver] = {
                "notes": f"Faro {ver} — release notes: https://github.com/{repo}/releases/tag/{tag}",
                "files": files,
            }

    catalog = {
        "description": DESCRIPTION,
        "maker": {"faro": {"name": "Faro"}},
        "device": [],
    }
    for device in DEVICES:
        board_versions = versions[device["board"]]
        if not board_versions:
            sys.exit(f"error: no firmware found for {device['name']} ({device['board']})")
        catalog["device"].append(
            {
                "maker": "faro",
                "class": "faro",
                "name": device["name"],
                "type": "esp32",
                "firmware": [
                    {
                        "class": "faro",
                        "role": "gui",
                        "title": "Faro",
                        "subTitle": device["subTitle"],
                        "version": board_versions,
                    }
                ],
            }
        )
    return catalog


def check_catalog(catalog, out_dir, base_url):
    errors = []
    if not base_url.startswith("https://"):
        errors.append("base URL must be https")
    for device in catalog["device"]:
        for firmware in device["firmware"]:
            if not firmware["version"]:
                errors.append(f"{device['name']}: no versions")
            for ver, entry in firmware["version"].items():
                for file in entry["files"]:
                    if not file["url"].startswith("https://"):
                        errors.append(f"{device['name']} {ver}: non-https URL {file['url']}")
                    local = out_dir / "firmware" / file["url"].split("/firmware/", 1)[1]
                    if not local.is_file():
                        errors.append(f"{device['name']} {ver}: missing {local}")
    return errors


def write_index(catalog, out_dir):
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Faro — MeshCore firmware catalog</title>
</head>
<body>
  <h1>Faro</h1>
  <p>{catalog["description"]}</p>
  <p><a href="catalog.json">catalog.json</a> — provider catalog for the Mesh America Device Configurator.</p>
  <p><a href="https://github.com/{DEFAULT_REPO}/releases">Firmware releases</a></p>
</body>
</html>
"""
    (out_dir / "index.html").write_text(html, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="_site", help="output directory (default: _site)")
    parser.add_argument("--base-url", required=True, help="public HTTPS base URL of the Pages site")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO))
    parser.add_argument("--releases", type=int, default=3, help="how many releases to publish (default: 3)")
    args = parser.parse_args()

    base_url = args.base_url.rstrip("/")
    if not base_url.startswith("https://"):
        sys.exit("error: --base-url must start with https://")

    out_dir = Path(args.out)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    releases = release_list(args.repo, args.releases + 5)[: args.releases]
    if not releases:
        sys.exit("error: no published releases found")

    with tempfile.TemporaryDirectory(prefix="faro-catalog-") as tmp:
        catalog = build_catalog(releases, args.repo, out_dir, base_url, Path(tmp))

    errors = check_catalog(catalog, out_dir, base_url)
    if errors:
        print("\n".join(f"error: {e}" for e in errors), file=sys.stderr)
        sys.exit(1)

    (out_dir / "catalog.json").write_text(
        json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    write_index(catalog, out_dir)
    print(f"wrote {out_dir}/catalog.json ({len(catalog['device'])} devices, {len(releases)} releases)")


if __name__ == "__main__":
    main()
