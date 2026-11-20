#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Restore the shebang line of every tracked script whose HEAD version had one.

Kept as a one-off repair after apply_header.py's first version deleted shebang
lines (they are comment lines, so its "remove the previous header" pass ate
them). apply_header.py is fixed; this exists so the damage is repairable from
the tree rather than by hand.

Idempotent: a file that already starts with `#!` is left alone.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    files = subprocess.run(["git", "ls-files", "*.sh", "*.py"],
                           cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    repaired = []
    for name in files:
        head = subprocess.run(["git", "show", f"HEAD:{name}"],
                              cwd=ROOT, capture_output=True, text=True)
        if head.returncode != 0:
            continue
        first = head.stdout.splitlines()[0] if head.stdout else ""
        if not first.startswith("#!"):
            continue
        path = ROOT / name
        text = path.read_text(encoding="utf-8")
        if text.splitlines()[0].startswith("#!"):
            continue
        path.write_text(first + "\n" + text, encoding="utf-8")
        repaired.append(name)

    for name in repaired:
        print("restored " + name)
    print(f"\n{len(repaired)} shebang(s) restored")
    return 0


if __name__ == "__main__":
    sys.exit(main())
