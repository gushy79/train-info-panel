#!/usr/bin/env python3
"""Build the firmware and package it as ONE flashable image for people who do not want ESP-IDF.

    source ~/esp/esp-idf/export.sh
    python3 tools/package_release.py        # -> dist/train-info-panel-<version>.bin  (+ .sha256)

The image holds the bootloader, partition table and app, merged at their real flash offsets, so it flashes
with a single command at address 0x0:

    python3 tools/provision.py --firmware dist/train-info-panel-<version>.bin     # image, then your settings
    # or, by hand:  esptool --chip esp32c6 write_flash 0x0 train-info-panel-<version>.bin

The image deliberately contains no settings and no secrets. Flashing it erases any settings already on the
panel (they live in a region the image overwrites with blanks), so always provision AFTER flashing; the
--firmware option does both in the right order.
"""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware"
BUILD = FW / "build"


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        sys.exit(f"failed: {' '.join(map(str, cmd))}")


def main():
    if not os.environ.get("IDF_PATH"):
        sys.exit("ESP-IDF is not active. Run:  source ~/esp/esp-idf/export.sh")
    run(["idf.py", "set-target", "esp32c6"] if not (BUILD / "CMakeCache.txt").exists() else ["idf.py", "--version"], FW)
    run(["idf.py", "build"], FW)

    version = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, capture_output=True, text=True).stdout.strip() or "dev"
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    out = dist / f"train-info-panel-{version}.bin"
    run([sys.executable, "-m", "esptool", "--chip", "esp32c6", "merge_bin", "-o", str(out), "@flash_args"], BUILD)

    digest = hashlib.sha256(out.read_bytes()).hexdigest()
    (dist / (out.name + ".sha256")).write_text(f"{digest}  {out.name}\n")
    print(f"\nbuilt {out.relative_to(ROOT)}  ({out.stat().st_size:,} bytes)\nsha256 {digest}")


if __name__ == "__main__":
    main()
