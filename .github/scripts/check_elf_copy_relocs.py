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

"""Refuse to ship an ELF whose data symbols are COPY-relocated out of a shared library.

WHY THIS EXISTS
---------------
`finc 1.0.0`'s x86_64-linux-gnu artifacts segfaulted before `main` for every user
whose machine carried a different `llvm-22` package than the CI runner. The
executable had 25 `R_X86_64_COPY` relocations landing inside `PT_GNU_RELRO`, and
libLLVM's static initializers write those globals after the loader makes the page
read-only. Neutralising the RELRO header in a byte copy made the identical binary
run, which is how the fault was proved to be ELF layout rather than a missing
library. Full write-up: docs/known-issues.md.

`--version` on the build machine cannot catch this: the trap only appears when
executable and library come from different builds. This check is structural, so
it fails on the build machine too.

WHAT IT ASSERTS
---------------
A COPY relocation for a symbol owned by a *non-libc* shared library is the defect
class. libc/stdlib symbols (`stdout`, `stderr`, `stdin`, `environ`, ...) are
excluded: their layout is a stable ABI guarantee, and a PIC build still emits
COPY relocations for them on every platform. Anything else -- LLVM vtables,
typeinfo, `llvm::OptimizationLevel` and friends -- must be reached through the
GOT (`R_*_GLOB_DAT`) instead.

Only meaningful for ELF targets; Windows and macOS are skipped by name.
"""

import shutil
import subprocess
import sys

# libc-owned data whose layout every ABI pins down. A COPY relocation for one of
# these is normal and present in PIC builds too.
ALLOWED = ("stdout", "stderr", "stdin", "environ", "__environ",
           "__progname", "__progname_full")
ALLOWED_PREFIXES = ("__libc_", "_IO_2_1_")


def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=False)


def readelf(flag, binary):
    """Run readelf with `flag` (which may hold several args) and return stdout."""
    for exe in ("readelf", "llvm-readelf"):
        if shutil.which(exe):
            r = run([exe] + flag.split() + [binary])
            if r.returncode == 0:
                return r.stdout
    return None


def main(argv):
    if len(argv) != 2:
        print("usage: check_elf_copy_relocs.py <binary>", file=sys.stderr)
        return 2
    binary = argv[1]

    header = readelf("-h", binary)
    if header is None:
        print(f"SKIP {binary}: no readelf available to inspect ELF headers")
        return 0
    if "ELF" not in header:
        # Windows PE / macOS Mach-O: this check does not apply.
        print(f"SKIP {binary}: not an ELF binary")
        return 0

    relocs = readelf("-rW", binary)
    sections = readelf("-SW", binary)
    if relocs is None:
        print(f"SKIP {binary}: readelf cannot dump relocations")
        return 0

    # A COPY relocation exists precisely because the symbol belongs to a shared
    # library; the loader copies it into storage the executable reserves. So
    # every COPY symbol here is library-owned by definition -- the only question
    # is whether it is a libc symbol (stable ABI) or something else.
    bad = []
    for line in relocs.splitlines():
        fields = line.split()
        if len(fields) < 5:
            continue
        if not any(f.startswith("R_") and f.endswith("_COPY") for f in fields):
            continue
        # <offset> <info> R_<arch>_COPY <value> <symbol>[@ver] + <addend>
        symbol = fields[4].split("@")[0]
        if symbol in ALLOWED or symbol.startswith(ALLOWED_PREFIXES):
            continue
        bad.append(symbol)

    if not bad:
        print(f"PASS {binary}: no non-libc COPY relocations")
        return 0

    # Name the section the offending relocations land in, so the message points
    # at the mechanism rather than only the symptom.
    relro = None
    for line in sections.splitlines():
        if ".data.rel.ro" in line or "GNU_RELRO" in line:
            fields = line.split()
            for i, f in enumerate(fields):
                if f.startswith("0") and len(f) > 4 and i + 1 < len(fields):
                    relro = fields[i]
                    break
            break

    print(f"FAIL {binary}: {len(bad)} non-libc COPY relocation(s)", file=sys.stderr)
    for symbol in sorted(set(bad))[:10]:
        print(f"  {symbol}", file=sys.stderr)
    if len(set(bad)) > 10:
        print(f"  ... and {len(set(bad)) - 10} more", file=sys.stderr)
    if relro:
        print(f"  (RELRO section starts at {relro})", file=sys.stderr)
    print(
        "A COPY relocation reserves writable storage in the executable for a\n"
        "symbol owned by a shared library. Whether that storage lands inside\n"
        "PT_GNU_RELRO depends on how the library was linked, so the binary can\n"
        "segfault on a host carrying a differently-built library. Build the\n"
        "objects as position-independent code so the symbol is reached through\n"
        "the GOT instead. See docs/known-issues.md.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
