#!/usr/bin/env python3
"""Runs games from data/resources/patches.txt with and without each of their patches.

Usage: test_patches.py --regtest <riverhorse-regtest> --outdir <dir> [--frames N] [--timeout S] <game files...>

For every game whose serial has patches, the game runs once without patches and once per patch for the same
number of frames. The last frame of each patched run is compared with the unpatched one. "changed" means the
patch altered the picture, "same" means it had no visible effect by that frame (it may only act later in the
game), and "failed" means the run crashed, produced no frame or ran longer than --timeout seconds.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES_FILE = os.path.join(ROOT, "data", "resources", "patches.txt")


def read_patches(path):
    patches = {}
    serial = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith(":"):
                serial = line[1:]
                patches.setdefault(serial, [])
            elif line.startswith("#") and serial:
                patches[serial].append(line[1:].split("\\")[-1])
    return patches


def run(regtest, game, outdir, frames, timeout, patch=None):
    os.makedirs(outdir, exist_ok=True)
    args = [regtest, "-renderer", "Software", "-log", "info", "-frames", str(frames), "-dumpinterval", str(frames),
            "-dumpdir", outdir]
    if patch:
        args += ["-patch", patch]
    args += ["--", game]
    try:
        returncode = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        returncode = None

    frame, serial = None, None
    for root, _, files in os.walk(outdir):
        pngs = sorted(f for f in files if f.startswith("frame_") and f.endswith(".png"))
        if pngs:
            frame = os.path.join(root, pngs[-1])
        if "riverhorse.log" in files:
            with open(os.path.join(root, "riverhorse.log"), encoding="utf-8", errors="replace") as log:
                match = re.search(r"Game Serial: (\S+)", log.read())
                if match:
                    serial = match.group(1)
    digest = None
    if returncode == 0 and frame:
        with open(frame, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
    return digest, serial


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--regtest", required=True)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--frames", type=int, default=3600)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("games", nargs="+")
    args = parser.parse_args()

    patches = read_patches(PATCHES_FILE)
    failures = 0
    for index, game in enumerate(args.games):
        base_dir = os.path.join(args.outdir, f"{index}-base")
        base_digest, serial = run(args.regtest, game, base_dir, args.frames, args.timeout)
        name = os.path.basename(game)
        if not base_digest:
            print(f"{name}: failed without patches", flush=True)
            failures += 1
            continue
        if serial not in patches:
            print(f"{name} ({serial}): no patches", flush=True)
            continue

        for patch_index, patch in enumerate(patches[serial]):
            digest, _ = run(args.regtest, game, os.path.join(args.outdir, f"{index}-{patch_index}"), args.frames,
                            args.timeout, patch)
            result = "failed" if not digest else ("changed" if digest != base_digest else "same")
            failures += (result == "failed")
            print(f"{name} ({serial}): {patch}: {result}", flush=True)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
