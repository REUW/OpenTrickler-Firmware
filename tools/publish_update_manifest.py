#!/usr/bin/env python3
"""Build a manifest.json + app.bin pair for the in-GUI "Check for Updates"
feature (Settings > Firmware Update).

The device checks GitHub directly over HTTPS for the latest release of your
repository, at the stable URLs:

    https://github.com/<owner>/<repo>/releases/latest/download/manifest.json
    https://github.com/<owner>/<repo>/releases/latest/download/app.bin

So publishing an update is just: build, run this script, then attach the two
files it produces to a GitHub Release. No server of your own, no secrets, no
SSH key -- GitHub hosts the files and the device's built-in TLS client (see
src/ota_tls.c) verifies GitHub's certificate before trusting anything it
downloads.

Typical use, right after a release build:

    python3 tools/publish_update_manifest.py \\
        --bin build-pico2w-release/app.bin \\
        --version "$(git describe --tags --always --dirty)" \\
        --notes "See CHANGELOG.md" \\
        --out-dir publish_out

Then either:
  - Manually: on the GitHub release page, attach publish_out/manifest.json
    and publish_out/app.bin as release assets, or
  - Automatically: let .github/workflows/publish-update-manifest.yml do it
    for you (it runs this script in CI and uploads both files to the
    triggering release using GitHub's own automatic GITHUB_TOKEN -- no
    secrets to configure).

The asset filenames matter: the device always asks for "manifest.json" and
"app.bin" by exact name, so --bin-name defaults to "app.bin" and should
normally be left alone.
"""

from __future__ import annotations

import argparse
import binascii
import json
import shutil
import sys
from pathlib import Path


def build_manifest(image: bytes, version: str, notes: str) -> dict:
    return {
        "version": version,
        "notes": notes,
        "size": len(image),
        "crc32": f"0x{binascii.crc32(image) & 0xFFFFFFFF:08X}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, dest="firmware_bin", help="Path to the built app.bin (not .uf2)")
    parser.add_argument("--version", required=True, help="Version string to advertise, e.g. output of 'git describe'")
    parser.add_argument("--notes", default="", help="Short changelog/notes text shown in the GUI")
    parser.add_argument("--out-dir", required=True, help="Directory to write manifest.json + app.bin into")
    parser.add_argument(
        "--bin-name",
        default="app.bin",
        help="Filename to publish the binary as (default: app.bin -- the device always fetches this exact "
        "asset name from the release, so only change this if you also change OTA_CLIENT_BIN_ASSET in "
        "src/ota_client.c)",
    )
    args = parser.parse_args()

    firmware_bin = Path(args.firmware_bin)
    if not firmware_bin.exists():
        parser.error(f"firmware .bin not found: {firmware_bin}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    dest_bin = out_dir / args.bin_name
    shutil.copyfile(firmware_bin, dest_bin)

    image = firmware_bin.read_bytes()
    manifest = build_manifest(image, args.version, args.notes)

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Wrote {dest_bin} ({manifest['size']} bytes, CRC32 {manifest['crc32']})")
    print(f"Wrote {manifest_path}:")
    print(json.dumps(manifest, indent=2))
    print()
    print("Attach both files to a GitHub Release (manually, or via")
    print(".github/workflows/publish-update-manifest.yml) and any device")
    print("pointed at this owner/repo will see the update.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
