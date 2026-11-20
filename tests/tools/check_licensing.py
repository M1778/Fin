#!/usr/bin/env python3
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

"""Fail when the license metadata in this tree stops agreeing with itself.

Fin ships under GPL-3.0-or-later. That is a fact about LICENSE, not a
preference: GPLv3 section 14 offers "any later version" only to a work that
carries the notice saying so, and LICENSE carries it (the "either version 3 of
the License, or (at your option) any later version" paragraph). Every other
declaration -- the SPDX header in each source file, the RPM and PKGBUILD
license fields, the license text inside each artifact -- is a restatement of
that, and a restatement that drifts is a defect that ships to users.

Four things are checked, one failure line per problem:

  1. every tracked source file carries an SPDX-License-Identifier line;
  2. the identifier is exactly the one LICENSE grants;
  3. the packaging metadata says the same thing, and each package format
     carries the license text where its own convention expects it;
  4. CMakeLists.txt installs LICENSE, so an archive cannot be built without
     it (the install rule is what puts it in the staged tree every artifact is
     repackaged from).

Usage: check_licensing.py [--root DIR]
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

SPDX = "GPL-3.0-or-later"

# The files that make up the shipped work.  Kept explicit rather than "every
# text file": documentation, samples and fixtures are not the licensed program,
# and a checker that demanded a header in each of them would be relaxed rather
# than satisfied.  This is the CI set plus the test sources the set had missed.
SOURCE_PATTERNS = [
    "src/*.cpp", "src/**/*.cpp", "src/*.hpp", "src/**/*.hpp",
    "src/lexer/lexer.l", "src/parser/parser.y",
    "finc/*.fin", "lib/std/*.fin",
    "tools/*/main.cpp",
    "tests/*.cpp", "tests/*.hpp",
]

# lexer.l puts its header inside the leading %{ block, so line 1 is not the
# only legal position; nothing in the tree has a reason to be later than this.
# The project header is thirty lines and carries the SPDX identifier at its foot,
# so the window must clear it. It stays bounded rather than scanning the whole
# file: an identifier buried in a code comment at line 400 is not a licence
# header, and accepting one would let a file drift out of compliance silently.
HEADER_WINDOW = 40
HEADER = re.compile(r"^\s*(?://|#)\s*SPDX-License-Identifier:\s*(\S+)\s*$", re.M)

# GPLv3 s14: without this phrase the grant is version 3 only, and the SPDX
# identifier above would be a false statement in every one of the source files.
LATER_VERSION_NOTICE = "either version 3 of the License, or (at your option) any later version"


def tracked(root, patterns):
    """The source set, from git when this is a checkout, else from the tree.

    A release tarball has no .git, and a packager running this checker there
    should get the same answer rather than a stack trace.
    """
    try:
        out = subprocess.run(["git", "ls-files", "-z", *patterns], cwd=root,
                             capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        found = set()
        for pattern in patterns:
            for path in root.glob(pattern):
                if path.is_file():
                    found.add(path.relative_to(root).as_posix())
        return sorted(found)
    return [name for name in out.decode("utf-8").split("\0") if name]


def check_headers(root, fail):
    files = tracked(root, SOURCE_PATTERNS)
    if not files:
        fail.append("no source files matched the tracked set; the checker is "
                    "looking at the wrong tree")
        return 0
    for name in files:
        text = (root / name).read_text(encoding="utf-8", errors="replace")
        head = "\n".join(text.splitlines()[:HEADER_WINDOW])
        match = HEADER.search(head)
        if not match:
            fail.append(f"{name}: no SPDX-License-Identifier in the first "
                        f"{HEADER_WINDOW} lines")
        elif match.group(1) != SPDX:
            fail.append(f"{name}: SPDX-License-Identifier is {match.group(1)}, "
                        f"LICENSE grants {SPDX}")
    return len(files)


def check_license_text(root, fail):
    lines = (root / "LICENSE").read_text(encoding="utf-8", errors="replace").splitlines()
    # Only the grant above the verbatim license text counts.  The GPL's own
    # "How to Apply These Terms" boilerplate contains the same "or any later
    # version" wording, so a whole-file substring search would pass on a file
    # that grants version 3 only.
    try:
        title = next(i for i, line in enumerate(lines)
                     if "GNU GENERAL PUBLIC LICENSE" in line)
    except StopIteration:
        fail.append("LICENSE: not the GNU General Public License text")
        return
    grant = " ".join(" ".join(lines[:title]).split())
    if SPDX not in grant:
        fail.append(f"LICENSE: the notice above the license text does not name "
                    f"SPDX {SPDX}")
    if LATER_VERSION_NOTICE not in grant:
        fail.append("LICENSE: no \"or (at your option) any later version\" "
                    "notice above the license text, so GPL-3.0-or-later is not "
                    "what it grants (GPLv3 s14); either restore the notice or "
                    "change SPDX to GPL-3.0-only everywhere -- source headers, "
                    "packaging, and the install rule -- as one change")


def require(path, needle, fail, why):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        fail.append(f"{path.name}: missing, so {why}")
        return
    if needle not in text:
        fail.append(f"{path.name}: {why} (expected to find {needle!r})")


def check_packaging(root, fail):
    packaging = root / "tools/packaging"

    # RPM: the License field, plus the text at the path RPM's own convention
    # uses (a %license file under /usr/share/licenses/<name>/).
    require(packaging / "build-rpm.sh", f'echo "License: {SPDX}"', fail,
            "RPM License field must name the license LICENSE grants")
    require(packaging / "build-rpm.sh", "/usr/share/licenses/finc/LICENSE", fail,
            "the RPM must carry the license text under /usr/share/licenses/finc/")
    require(packaging / "build-rpm.sh", "%license /usr/share/licenses/finc/LICENSE",
            fail, "the RPM must mark the text with %license, or rpm -qlp lists "
                  "it as an ordinary file and Fedora's license policy is unmet")
    require(packaging / "build-rpm.sh", '"$stage/lib/finc/LICENSE"', fail,
            "the RPM must take the license from the staged tree cmake installed")

    # Debian: no License field in control, but policy puts the text in
    # /usr/share/doc/<package>/copyright.
    require(packaging / "build-deb.sh", "usr/share/doc/finc/copyright", fail,
            "the .deb must carry the license text as "
            "/usr/share/doc/finc/copyright")
    require(packaging / "build-deb.sh", '"$stage/lib/finc/LICENSE"', fail,
            "the .deb must take the license from the staged tree cmake installed")

    # Arch: license= is the SPDX expression makepkg validates against.
    pkgbuild = (packaging / "PKGBUILD").read_text(encoding="utf-8",
                                                  errors="replace")
    match = re.search(r"^license=\(([^)]*)\)", pkgbuild, re.M)
    if not match:
        fail.append("PKGBUILD: no license=() line")
    elif SPDX not in match.group(1):
        fail.append(f"PKGBUILD: license=({match.group(1)}) disagrees with "
                    f"LICENSE, which grants {SPDX}")
    if "usr/share/licenses/" not in pkgbuild:
        fail.append("PKGBUILD: package() must install the license text under "
                    "/usr/share/licenses/finc-bin/")
    if re.search(r"^sha256sums=\(\s*'SKIP'", pkgbuild, re.M):
        fail.append("PKGBUILD: sha256sums=('SKIP') installs unverified bytes; "
                    "fill it from the release's .sha256 sidecar (see the comment "
                    "above pkgname)")

    # macOS: a dmg has no license metadata, so the text sits at the volume root.
    require(packaging / "build-dmg.sh", 'cp "$license" "$root/LICENSE"', fail,
            "the .dmg must carry the license text at the volume root")


def check_cmake_install(root, fail):
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    rules = re.findall(r"install\(\s*FILES\b[^)]*\)", text, re.S)
    if not any("LICENSE" in rule for rule in rules):
        fail.append("CMakeLists.txt: no install(FILES ... LICENSE ...) rule, so "
                    "cmake --install stages no license text and every archive, "
                    ".deb, .rpm, .dmg and setup exe ships without one")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2],
                        help="repository root (default: this checkout)")
    args = parser.parse_args()
    root = args.root.resolve()
    fail = []

    count = check_headers(root, fail)
    check_license_text(root, fail)
    check_packaging(root, fail)
    check_cmake_install(root, fail)

    for line in fail:
        print(f"FAIL {line}")
    if fail:
        raise SystemExit(f"{len(fail)} licensing defect(s)")
    print(f"PASS licensing: {count} source files carry SPDX {SPDX}; LICENSE, "
          f"CMake install rule, rpm/deb/PKGBUILD/dmg agree")


if __name__ == "__main__":
    sys.exit(main())
