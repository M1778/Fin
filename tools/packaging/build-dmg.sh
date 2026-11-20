#!/usr/bin/env bash
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

# macOS .dmg from a staged release tree (bin/finc + lib/std/**).
#
# Usage: build-dmg.sh <stage-dir> <version> <target-triple> <output.dmg>
#
# The dmg carries the same bin/finc + lib/std tree the portable tarball does,
# under one top-level folder, so the compiler's <exe-dir>/../lib/std rule
# holds wherever the user drags it. macOS-only (hdiutil); there is no local
# proof off a Mac -- CI attaches the image and runs finc --version from it.
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: build-dmg.sh <stage-dir> <version> <target-triple> <output.dmg>" >&2
  exit 1
fi
stage="$1"; version="$2"; target="$3"; out="$4"

command -v hdiutil >/dev/null 2>&1 || { echo "build-dmg.sh: hdiutil not found (macOS only)" >&2; exit 1; }
[ -f "$stage/bin/finc" ] || { echo "build-dmg.sh: $stage/bin/finc is missing" >&2; exit 1; }
[ -n "$(find "$stage/lib/std" -name '*.fin' -print -quit 2>/dev/null)" ] \
  || { echo "build-dmg.sh: $stage/lib/std/**.fin is missing" >&2; exit 1; }
# GPL-3.0 §4/§6 require conveying a copy of the license with the object code.
license="$stage/lib/finc/LICENSE"
[ -f "$license" ] \
  || { echo "build-dmg.sh: $license is missing (cmake --install should have put it there)" >&2; exit 1; }

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
root="$work/finc-$version-$target"
mkdir -p "$root"
cp -R "$stage/bin" "$stage/lib" "$root/"
# A dmg has no package metadata to carry a license field, so the text sits
# beside bin/ and lib/ in the dmg's named folder the user drags from.
cp "$license" "$root/LICENSE"
# `-srcfolder` copies the named directory's *contents* into the volume root,
# so pass the wrapper's parent: the volume must carry the named folder, not
# that folder's bin/lib tree laid flat.
hdiutil create -volname "finc $version" -srcfolder "$work" -ov -format UDZO "$out"
echo "wrote $out ($(du -h "$out" | cut -f1))"
