#!/usr/bin/env python3
"""Assert that a built binary's machine type matches the target triple it will
ship under.

ADR 0010: `finn`'s download.rs:62-65 matches release assets by OS substring
alone, so an arm64 user silently receives an x86_64 build.  Naming the archives
with both OS and architecture fixes the matching, but a name is only worth the
bytes behind it -- a release job that labels an x86_64 binary `aarch64-` would
reintroduce exactly the bug the naming scheme exists to make impossible.  So the
machine field in the executable header is read and compared, rather than
trusting that the runner label meant what it said.

Usage: check_arch.py <path-to-binary> <rust-target-triple>
"""

import struct
import sys

# Machine identifiers per container format, keyed by the architecture as it is
# spelled in a Rust target triple.
ELF_MACHINE = {0x3E: "x86_64", 0xB7: "aarch64", 0x03: "i686", 0xF3: "riscv64"}
MACHO_CPUTYPE = {0x01000007: "x86_64", 0x0100000C: "aarch64", 0x00000007: "i686"}
PE_MACHINE = {0x8664: "x86_64", 0xAA64: "aarch64", 0x014C: "i686", 0x01C4: "arm"}


def machine_of(path):
    """Return the architecture the binary is built for, and the format it is in."""
    with open(path, "rb") as handle:
        head = handle.read(0x40)
        if len(head) < 0x40:
            raise ValueError("%s is too short to be an executable" % path)

        if head[:4] == b"\x7fELF":
            (e_machine,) = struct.unpack_from("<H", head, 18)
            return ELF_MACHINE.get(e_machine, "elf:0x%x" % e_machine), "ELF"

        if head[:4] in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
            (cputype,) = struct.unpack_from("<I", head, 4)
            return MACHO_CPUTYPE.get(cputype, "macho:0x%x" % cputype), "Mach-O"

        if head[:4] in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
            # A universal binary carries several architectures; the release
            # archives are per-target, so one is what is expected.
            raise ValueError("%s is a universal binary; one target per archive" % path)

        if head[:2] == b"MZ":
            (e_lfanew,) = struct.unpack_from("<I", head, 0x3C)
            handle.seek(e_lfanew)
            pe = handle.read(6)
            if pe[:4] != b"PE\0\0":
                raise ValueError("%s has an MZ header but no PE signature" % path)
            (machine,) = struct.unpack_from("<H", pe, 4)
            return PE_MACHINE.get(machine, "pe:0x%x" % machine), "PE"

    raise ValueError("%s is in no executable format this script knows" % path)


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    path, triple = argv[1], argv[2]

    expected = triple.split("-", 1)[0]
    # Rust spells 64-bit ARM `aarch64` everywhere; the formats do not agree with
    # each other, which is why the tables above normalise to the Rust spelling.
    actual, container = machine_of(path)

    print("%s: %s binary for %s (target %s expects %s)"
          % (path, container, actual, triple, expected))
    if actual != expected:
        sys.exit("::error::%s is a %s binary but is about to ship as %s"
                 % (path, actual, triple))


if __name__ == "__main__":
    main(sys.argv)
