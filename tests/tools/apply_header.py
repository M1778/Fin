# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️ for someone special.
#
# This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
# Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
# Fin is the name of the Angel from BlackNightmare-Stories not yet published.
# “Love is the reason this project exists”
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778/Fin
# Profile: https://github.com/M1778M/
# -----------------------------------------------------------------------------
# Copyright (C) 2027 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================

"""Apply the project header to every source file that should carry it.

Run with no arguments to apply; `--check` reports what would change without
writing (use it in CI once the tree is converted).

WHY A SCRIPT RATHER THAN A SED LOOP
-----------------------------------
Three things have to be right in 470 files, and each is easy to get wrong once:

  * the comment spelling differs (// for C++ and Fin, # for Python and shell);
  * a shebang must stay the first line or the script stops being executable;
  * an existing SPDX line must be replaced, not duplicated -- check_licensing.py
    reads the identifier from the header, and two of them is a defect.

SCOPE
-----
Everything under src/, lib/std/, finc/, tests/ (except tests/samples/), tools/,
bench/, editors/, and the repository root scripts. tests/samples/ is excluded
deliberately: those files ARE the language specification (ADR 0008), and four of
them pin diagnostic line numbers in `//@ error L:C` that a thirty-line header
would silently invalidate.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REPO_URL = "https://github.com/M1778/Fin"
PROFILE_URL = "https://github.com/M1778M/"
COPYRIGHT_YEAR = "2027"

# The header body, without comment markers. One entry per line; empty strings
# become bare comment lines.
BODY = [
    "Fin Programming Language Compiler",
    "",
    "Made with \u2764\ufe0f for someone special.",
    "",
    "This project is genuinely built on love and dedication, a way to express your love can be done through Fin.",
    "Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.",
    "Fin is the name of the Angel from BlackNightmare-Stories not yet published.",
    "\u201cLove is the reason this project exists\u201d",
    "-" * 77,
    "Author: M1778",
    f"Repository: {REPO_URL}",
    f"Profile: {PROFILE_URL}",
    "-" * 77,
    f"Copyright (C) {COPYRIGHT_YEAR} M1778",
    "",
    "This file is part of the Fin Programming Language Compiler.",
    "",
    "Fin is free software: you can redistribute it and/or modify",
    "it under the terms of the GNU General Public License as published by",
    "the Free Software Foundation, either version 3 of the License, or",
    "(at your option) any later version.",
    "",
    "Fin is distributed in the hope that it will be useful,",
    "but WITHOUT ANY WARRANTY; without even the implied warranty of",
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the",
    "GNU General Public License for more details.",
    "",
    "You should have received a copy of the GNU General Public License",
    "along with Fin.  If not, see <https://www.gnu.org/licenses/>.",
]

# Every file carries the licence identifier too; check_licensing.py reads it
# from the header, so it lives here rather than as a separate first line.
SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"

SUFFIXES = {
    ".cpp": "//", ".hpp": "//", ".h": "//", ".cc": "//",
    ".fin": "//",
    ".py": "#", ".sh": "#",
}

EXCLUDED_DIRS = {
    "tests/samples",        # the language specification; see the docstring
    "tests/contract-corpus",  # fixture data whose expected output pins line/column
    "build", "build-asan", "build-pic", "build-stage", "build-explain",
    "build-const", "build-ci", "legacy", "pyprototype",
    ".git", "node_modules", "opendesign",
}

# A header block is recognised by this line; a file that already has one is
# rewritten in place rather than stacked.
MARKER_RE = re.compile(r"Fin Programming Language Compiler\s*$")
SPDX_RE = re.compile(r"(?m)^\s*(?://|#)\s*SPDX-License-Identifier:.*\n")


def in_scope(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    for excluded in EXCLUDED_DIRS:
        if rel == excluded or rel.startswith(excluded + "/"):
            return False
    return path.suffix in SUFFIXES


def candidates():
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or not in_scope(path):
            continue
        yield path


def strip_old_header(text: str, marker: str) -> str:
    """Remove a previous copy of this header, and any standalone SPDX line.

    The shebang is lifted first and put back by the caller: this function
    deletes a run of comment lines, and `#!` is a comment line, so a header
    that had been prepended ABOVE the shebang would otherwise take it with it
    and leave the script unrunnable. That happened once; the caller now always
    re-prepends it.
    """
    lines = text.splitlines(keepends=True)
    shebang = []
    while lines and (lines[0].startswith("#!") or
                     (len(shebang) < 2 and re.match(r"^#.*coding[:=]", lines[0]))):
        shebang.append(lines.pop(0))
    # A prior header is a contiguous block of comment lines containing the
    # marker line, running from the first such run to the next non-comment line.
    start = None
    for i, line in enumerate(lines):
        if line.lstrip().startswith(marker) and MARKER_RE.search(line.rstrip("\n")):
            # Walk back to the beginning of the comment run, but never past a
            # line that is not a comment.
            j = i
            while j > 0 and lines[j - 1].lstrip().startswith(marker):
                j -= 1
            start = j
            break
    if start is not None:
        end = start
        while end < len(lines) and lines[end].lstrip().startswith(marker):
            end += 1
        del lines[start:end]
    text = "".join(shebang) + "".join(lines)
    return SPDX_RE.sub("", text)


def build_header(marker: str) -> str:
    out = []
    out.append(f"{marker} {'=' * 77}")
    for line in BODY:
        out.append(f"{marker} {line}".rstrip() if line else f"{marker}")
    # SPDX goes last so the licence identifier sits beside the notice it restates.
    out.append(f"{marker}")
    out.append(f"{marker} {SPDX}")
    out.append(f"{marker} {'=' * 77}")
    out.append("")
    return "\n".join(out) + "\n"


def want_text(path: Path, text: str) -> str:
    marker = SUFFIXES[path.suffix]
    body = strip_old_header(text, marker)
    header = build_header(marker)
    # A shebang, or a Python encoding declaration, must remain the first line.
    lines = body.splitlines(keepends=True)
    prefix = []
    while lines and (lines[0].startswith("#!") or
                     (len(prefix) < 2 and re.match(r"^#.*coding[:=]", lines[0]))):
        prefix.append(lines.pop(0))
    prefix_text = "".join(prefix)
    if prefix_text and not prefix_text.endswith("\n"):
        prefix_text += "\n"
    rest = "".join(lines).lstrip("\n")
    return prefix_text + header + rest


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would change; write nothing")
    args = ap.parse_args(argv[1:])

    changed, unchanged, errors = [], 0, []
    for path in candidates():
        try:
            original = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError) as exc:
            errors.append(f"{path.relative_to(ROOT)}: {exc}")
            continue
        updated = want_text(path, original)
        if updated == original:
            unchanged += 1
            continue
        changed.append(path.relative_to(ROOT).as_posix())
        if not args.check:
            path.write_text(updated, encoding="utf-8")

    for rel in changed:
        print(("would update " if args.check else "updated ") + rel)
    for err in errors:
        print("SKIPPED " + err, file=sys.stderr)
    verb = "would change" if args.check else "changed"
    print(f"\n{len(changed)} {verb}, {unchanged} already current, {len(errors)} skipped")
    return 1 if (args.check and changed) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
