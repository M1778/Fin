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

# Fedora .rpm from a staged release tree (bin/finc + lib/std/**).
#
# Usage: build-rpm.sh <stage-dir> <version> <target-triple> <output.rpm> [requires]
#
# Unlike the .deb (tools/packaging/build-deb.sh), an .rpm cannot be assembled
# from `ar` and `tar` -- the format wraps a cpio payload with a binary header
# only rpmbuild writes. So this needs `rpmbuild` (release.yml installs the
# `rpm` package on the Ubuntu build host; the rpm it produces still targets
# Fedora, because a noarch-style binary package carries no host content beyond
# the staged tree). There is no local proof for this script off an rpm host;
# CI proves it (rpm -qlp, unpack via rpm2cpio, run finc --version).
#
# Layout is FHS for the same reason as the .deb: /usr/bin/finc resolves
# /usr/lib/std through the compiler's <exe-dir>/../lib/std rule, so the
# installed binary needs no wrapper and no environment variable.
set -euo pipefail

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo "usage: build-rpm.sh <stage-dir> <version> <target-triple> <output.rpm> [requires]" >&2
  exit 1
fi
stage="$1"; version="$2"; target="$3"; out="$4"; requires="${5:-}"

command -v rpmbuild >/dev/null 2>&1 || { echo "build-rpm.sh: rpmbuild not found (apt-get install rpm)" >&2; exit 1; }
[ -f "$stage/bin/finc" ] || { echo "build-rpm.sh: $stage/bin/finc is missing" >&2; exit 1; }
[ -n "$(find "$stage/lib/std" -name '*.fin' -print -quit 2>/dev/null)" ] \
  || { echo "build-rpm.sh: $stage/lib/std/**.fin is missing" >&2; exit 1; }
# GPL-3.0 §4/§6 require conveying a copy of the license with the object code.
license="$stage/lib/finc/LICENSE"
[ -f "$license" ] \
  || { echo "build-rpm.sh: $license is missing (cmake --install should have put it there)" >&2; exit 1; }

case "${target%%-*}" in
  x86_64) rpm_arch=x86_64 ;;
  aarch64) rpm_arch=aarch64 ;;
  *) echo "build-rpm.sh: no RPM arch for target $target" >&2; exit 1 ;;
esac

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cp "$stage/bin/finc" "$work/SOURCES/finc"
cp "$stage"/lib/std/*.fin "$work/SOURCES/"
cp "$license" "$work/SOURCES/LICENSE"
{
  echo "Name: finc"
  echo "Version: $version"
  echo "Release: 1"
  echo "Summary: Fin systems programming language compiler"
  echo "License: GPL-3.0-or-later"
  echo "BuildArch: $rpm_arch"
  [ -z "$requires" ] || echo "Requires: $requires"
  echo "%description"
  echo "The Fin compiler (finc) with its standard library."
  echo "The binary finds its library relative to itself"
  echo "(<exe-dir>/../lib/std), so /usr/bin/finc reads /usr/lib/std."
  echo "%install"
  echo 'mkdir -p "%{buildroot}/usr/bin" "%{buildroot}/usr/lib/std" "%{buildroot}/usr/share/licenses/finc"'
  echo 'install -m755 "%{_sourcedir}/finc" "%{buildroot}/usr/bin/finc"'
  echo 'install -m644 %{_sourcedir}/*.fin "%{buildroot}/usr/lib/std/"'
  echo 'install -m644 "%{_sourcedir}/LICENSE" "%{buildroot}/usr/share/licenses/finc/LICENSE"'
  echo "%files"
  echo "/usr/bin/finc"
  echo "/usr/lib/std/"
  echo "%license /usr/share/licenses/finc/LICENSE"
} > "$work/SPECS/finc.spec"
rpmbuild -bb --define "_topdir $work" "$work/SPECS/finc.spec"
built="$(find "$work/RPMS" -name '*.rpm' -print -quit)"
[ -n "$built" ] || { echo "build-rpm.sh: rpmbuild produced no .rpm" >&2; exit 1; }
cp "$built" "$out"
echo "wrote $out ($(du -h "$out" | cut -f1))"
