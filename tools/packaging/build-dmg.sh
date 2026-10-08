#!/usr/bin/env bash
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

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
root="$work/finc-$version-$target"
mkdir -p "$root"
cp -R "$stage/bin" "$stage/lib" "$root/"
hdiutil create -volname "finc $version" -srcfolder "$root" -ov -format UDZO "$out"
echo "wrote $out ($(du -h "$out" | cut -f1))"
