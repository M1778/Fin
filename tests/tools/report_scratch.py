#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Report scratch and build directories that are large or stale.

`~/fin-tmp/` reached 8.9 GB because every agent left its build trees behind and
nothing ever looked. This does not delete anything -- a directory under scratch
may be a live build another process is writing to, and one Conan toolchain there
is referenced by absolute path from `build-asan`'s CMake cache. It prints what
is reclaimable and how old it is, so a human or an agent decides.

Usage:
    python3 tests/tools/report_scratch.py            # report only
    python3 tests/tools/report_scratch.py --min-gb 0.5 --days 1
"""

import argparse
import os
import shutil
import sys
import time
from pathlib import Path

HOME = Path.home()

# Roots checked, with the note that explains why each exists.
ROOTS = [
    (HOME / "fin-tmp", "agent scratch"),
    (HOME / "Fin" / "build", "Debug build tree"),
    (HOME / "Fin" / "build-asan", "sanitizer tree"),
    (HOME / "Fin" / "build-stage", "stage-coverage tree"),
    (HOME / "Fin" / "build-explain", "diagnostics tree"),
    (HOME / "Fin" / "build-pic", "PIC proof tree"),
    (HOME / "Fin" / "build" / "release-check", "release rehearsal"),
    (HOME / "finn" / "target", "Rust build cache"),
]


def human(n):
    for unit in ("B", "K", "M", "G"):
        if n < 1024:
            return f"{n:.0f}{unit}"
        n /= 1024
    return f"{n:.1f}T"


def newest_mtime(path):
    """Newest mtime anywhere under `path`, or the directory's own."""
    latest = path.stat().st_mtime
    for base, dirs, files in os.walk(path):
        # Do not descend into a chroot's /proc; it is not ours and it is huge.
        if "proc" in dirs and (Path(base) / "rootfs").exists():
            dirs.remove("proc")
        for name in files:
            try:
                latest = max(latest, os.lstat(os.path.join(base, name)).st_mtime)
            except OSError:
                pass
    return latest


def walk_size(entry):
    """Bytes under `entry`, never descending into `proc` or `sys`.

    Those are kernel-backed: a chroot's `proc/kcore` reports 140 TB and each
    `sys/devices/.../resource*` 8 GB, so counting them made a 2 GB rootfs read
    as 18 TB. They occupy no disk.
    """
    total = 0
    for base, dirs, files in os.walk(entry):
        for virtual in ("proc", "sys", "dev"):
            if virtual in dirs:
                dirs.remove(virtual)
        for name in files:
            try:
                total += os.lstat(os.path.join(base, name)).st_size
            except OSError:
                pass
    return total


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-gb", type=float, default=0.25,
                    help="only report entries at least this large (default 0.25)")
    ap.add_argument("--days", type=float, default=1.0,
                    help="flag entries untouched for this many days (default 1)")
    args = ap.parse_args(argv[1:])

    now = time.time()
    limit = int(args.min_gb * 1024 ** 3)
    rows = []
    for root, note in ROOTS:
        if not root.exists():
            continue
        for entry in sorted(root.iterdir()):
            try:
                if not entry.is_dir():
                    size = entry.stat().st_size
                    if size < limit:
                        continue
                else:
                    size = walk_size(entry)
            except OSError:
                continue
            if size < limit:
                continue
            age_days = (now - newest_mtime(entry)) / 86400 if entry.is_dir() else \
                (now - entry.stat().st_mtime) / 86400
            rows.append((size, age_days, str(entry), note))

    if not rows:
        print(f"nothing above {args.min_gb} GB")
        return 0

    rows.sort(reverse=True)
    print(f"{'SIZE':>8}  {'UNTOUCHED':>10}  PATH  (kind)")
    stale_bytes = 0
    for size, age, path, note in rows:
        flag = "stale" if age >= args.days else "live "
        print(f"{human(size):>8}  {age:6.1f}d {flag}  {path}  ({note})")
        if age >= args.days:
            stale_bytes += size
    print()
    print(f"reclaimable if untouched for >{args.days:g}d: {human(stale_bytes)}")
    print("This tool deletes nothing. Check for a live build before removing;")
    print("~/fin-tmp/sw-conan is referenced by build-asan/CMakeCache.txt.")
    return 0


def _safe(path):
    try:
        return os.lstat(path).st_size >= 0
    except OSError:
        return False


if __name__ == "__main__":
    sys.exit(main(sys.argv))
