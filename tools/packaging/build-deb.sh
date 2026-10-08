#!/usr/bin/env bash
# Ubuntu .deb from a staged release tree (bin/finc + lib/std/**).
#
# Usage: build-deb.sh <stage-dir> <version> <target-triple> <output.deb> [depends]
#
# A .deb is an `ar` archive holding `debian-binary`, `control.tar.gz` and
# `data.tar.xz`, so this needs only `ar` and `tar` -- the same script builds
# the package on the release runner and on a maintainer's machine, and it is
# proven the same way in both places (see below). `dpkg-deb` is never required.
#
# Layout is FHS: /usr/bin/finc with the library at /usr/lib/std. That is not a
# second layout to keep in step: the compiler locates its library as
# <exe-dir>/../lib/std (src/driver/SearchPaths.hpp), so /usr/bin/finc resolves
# /usr/lib/std by the same rule the portable archive's bin/finc uses for
# lib/std.
#
# `depends` (e.g. `libllvm22`) becomes the Depends line when the binary links
# the LLVM shared library; empty (the static musl case, or a statically linked
# build) means no Depends line at all. The caller decides by reading `ldd`,
# not by guessing -- see release.yml.
set -euo pipefail

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo "usage: build-deb.sh <stage-dir> <version> <target-triple> <output.deb> [depends]" >&2
  exit 1
fi
stage="$1"; version="$2"; target="$3"; out="$4"; depends="${5:-}"

for tool in ar tar xz; do
  command -v "$tool" >/dev/null 2>&1 || { echo "build-deb.sh: required tool '$tool' not found" >&2; exit 1; }
done
[ -f "$stage/bin/finc" ] || { echo "build-deb.sh: $stage/bin/finc is missing" >&2; exit 1; }
[ -n "$(find "$stage/lib/std" -name '*.fin' -print -quit 2>/dev/null)" ] \
  || { echo "build-deb.sh: $stage/lib/std/**.fin is missing" >&2; exit 1; }

case "${target%%-*}" in
  x86_64) deb_arch=amd64 ;;
  aarch64) deb_arch=arm64 ;;
  *) echo "build-deb.sh: no Debian arch for target $target" >&2; exit 1 ;;
esac

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
pkg="$work/pkg"; mkdir -p "$pkg/DEBIAN" "$pkg/usr/bin" "$pkg/usr/lib/std"
cp "$stage/bin/finc" "$pkg/usr/bin/finc"
chmod 755 "$pkg/usr/bin/finc"
cp "$stage"/lib/std/*.fin "$pkg/usr/lib/std/"
chmod 644 "$pkg"/usr/lib/std/*.fin

{
  echo "Package: finc"
  echo "Version: $version"
  echo "Architecture: $deb_arch"
  echo "Maintainer: Fin Contributors <https://github.com/M1778/Fin>"
  [ -z "$depends" ] || echo "Depends: $depends"
  echo "Description: Fin systems programming language compiler"
  echo " The Fin compiler (finc) with its standard library."
  echo " ."
  echo " The binary finds its library relative to itself"
  echo " (<exe-dir>/../lib/std), so /usr/bin/finc reads /usr/lib/std."
} > "$pkg/DEBIAN/control"

printf '2.0\n' > "$work/debian-binary"
tar -czf "$work/control.tar.gz" -C "$pkg/DEBIAN" control
tar -cJf "$work/data.tar.xz" -C "$pkg" usr
ar rcs "$out" "$work/debian-binary" "$work/control.tar.gz" "$work/data.tar.xz"
echo "wrote $out ($(du -h "$out" | cut -f1))"
